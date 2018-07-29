/*

The MIT License (MIT)

Copyright (c) 2017 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "acoustics2D.h"

void acousticsSourceSetup2D(mesh2D *mesh, occa::kernelInfo &kernelInfo) {

  // location of source
  dfloat x0 = 0.f; dfloat y0 = 0.35;
  mesh->sourceX0 = x0;
  mesh->sourceY0 = y0;

  // size of source injection patch
  dfloat patchsize = 0.1;

  //frequency and time shift of the riker pulse
  mesh->sourceFreq = 4.0;
  mesh->sourceT0 = -0.1;

  //We want to collect a patch of elements around the source point and solve for 
  //  the scattered field in that patch. We need to construct a list of these elements and
  //  flag what faces are interfaces between the usual domain and the scattered field. 

  //find all elements within a certain distance from the source point, 
  // and find the element which contains the source point
  int sourceId = -1;
  mesh->sourceNelements = 0;
  mesh->MRABsourceNelements = (int *) calloc(mesh->MRABNlevels,sizeof(int));

  int *patchFlag = (int *) calloc(mesh->Nelements+mesh->totalHaloPairs,sizeof(int));

  dfloat mindist = 1e9;
  for (int e=0;e<mesh->Nelements;e++) {
    int id = e*mesh->Nverts;

    dfloat x1 = mesh->EX[id+0]; /* x-coordinates of vertices */
    dfloat x2 = mesh->EX[id+1];
    dfloat x3 = mesh->EX[id+2];

    dfloat y1 = mesh->EY[id+0]; /* y-coordinates of vertices */
    dfloat y2 = mesh->EY[id+1];
    dfloat y3 = mesh->EY[id+2];

    dfloat dist1 = sqrt((x1-x0)*(x1-x0) + (y1-y0)*(y1-y0));
    dfloat dist2 = sqrt((x2-x0)*(x2-x0) + (y2-y0)*(y2-y0));
    dfloat dist3 = sqrt((x3-x0)*(x3-x0) + (y3-y0)*(y3-y0));

    if ((dist1<=patchsize)||(dist2<=patchsize)||(dist3<=patchsize)) {
      //this element is close to the source point
      mesh->sourceNelements++;
      mesh->MRABsourceNelements[mesh->MRABlevel[e]]++;
      patchFlag[e] = 1;
    } else {
      continue;
    }

    //find the cubature node which is closest to the source point and use the c2 from that node
    for(int n=0;n<mesh->cubNp;++n){
      // cubature node coordinates
      dfloat rn = mesh->cubr[n];
      dfloat sn = mesh->cubs[n];

      /* physical coordinate of interpolation node */
      dfloat x = -0.5*(rn+sn)*x1 + 0.5*(1+rn)*x2 + 0.5*(1+sn)*x3;
      dfloat y = -0.5*(rn+sn)*y1 + 0.5*(1+rn)*y2 + 0.5*(1+sn)*y3;

      dfloat dist = sqrt((x-x0)*(x-x0) + (y-y0)*(y-y0));

      if (dist < mindist) {
        mindist = dist;
        //record speed of sound at this point
        #if WADG
          mesh->sourceC2 = mesh->c2[n + e*mesh->cubNp];
        #else
          mesh->sourceC2 = 1.f;
        #endif
      }
    }
  }

  //halo exchange the patch flag
  if (mesh->totalHaloPairs) {
    int *sendbuffer = (int *) calloc(mesh->totalHaloPairs,sizeof(int));
    meshHaloExchange(mesh,sizeof(int),patchFlag,sendbuffer,patchFlag+mesh->Nelements);
    free(sendbuffer);
  }

  //create the element list and flag interfaces 
  mesh->sourceElements = (int*) calloc(mesh->sourceNelements,sizeof(int));

  int cnt = 0;
  for (int e=0;e<mesh->Nelements;e++) {
    if (patchFlag[e]==1) {
      //record this element
      mesh->sourceElements[cnt++] = e;

      //this element is in the patch. Check the neighbours
      for (int f=0;f<mesh->Nfaces;f++) {
        int eP = mesh->EToE[e*mesh->Nfaces + f];

        int flagP =1;
        if (eP >-1) flagP = patchFlag[eP];

        //if neighbour isnt in the source patch, flag it in EToB
        if (flagP==0) mesh->EToB[e*mesh->Nfaces+f] = -10;
      }
    } else {
      //this element isnt in the patch. Check the neighbours
      for (int f=0;f<mesh->Nfaces;f++) {
        int eP = mesh->EToE[e*mesh->Nfaces + f];

        int flagP =0;
        if (eP >-1) flagP = patchFlag[eP];

        //if neighbour is in the source patch, flag it in EToB
        if (flagP==1) mesh->EToB[e*mesh->Nfaces+f] = -11;
      }
    }
  }

  //create 1D BB modal projection from the 2D invVB
  mesh->invVB1D = (dfloat *) calloc(mesh->Nfp*mesh->Nfp, sizeof(dfloat));
  dfloat *invVB1DT = (dfloat *) calloc(mesh->Nfp*mesh->Nfp, sizeof(dfloat));
  for (int n=0;n<mesh->Nfp;n++) {
    for (int m=0;m<mesh->Nfp;m++) {
      mesh->invVB1D[m*mesh->Nfp+n] = mesh->invVB[m*mesh->Np+n];
      invVB1DT[n*mesh->Nfp+m] = mesh->invVB[m*mesh->Np+n];
    }
  }
  mesh->o_EToB.copyFrom(mesh->EToB); //update boundary flags
  mesh->o_invVB1DT = mesh->device.malloc(mesh->Nfp*mesh->Nfp*sizeof(dfloat),invVB1DT);

  kernelInfo.addDefine("p_pmlNfields", mesh->pmlNfields);
  kernelInfo.addDefine("p_sourceX0", mesh->sourceX0);
  kernelInfo.addDefine("p_sourceY0", mesh->sourceY0);
  kernelInfo.addDefine("p_sourceT0", mesh->sourceT0);
  kernelInfo.addDefine("p_sourceFreq", mesh->sourceFreq);
  kernelInfo.addDefine("p_sourceC2", mesh->sourceC2);

  char *pointSourceFileName = strdup(DHOLMES "/examples/acousticsTri2Dbbdg/rickerPulse2D.h");
  kernelInfo.addInclude(pointSourceFileName);

  printf("Source: found %d elements inside source injection patch\n", mesh->sourceNelements);

  free(patchFlag);
  free(invVB1DT);
}