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

void acousticsPmlSetup2D(mesh2D *mesh){

  //constant pml absorption coefficient
  dfloat xsigma = 80, ysigma = 80;

  //construct element and halo lists
  mesh->MRABpmlNelements = (int *) calloc(mesh->MRABNlevels,sizeof(int));
  mesh->MRABpmlElementIds = (int **) calloc(mesh->MRABNlevels,sizeof(int*));
  mesh->MRABpmlIds = (int **) calloc(mesh->MRABNlevels, sizeof(int*));

  mesh->MRABpmlNhaloElements = (int *) calloc(mesh->MRABNlevels,sizeof(int));
  mesh->MRABpmlHaloElementIds = (int **) calloc(mesh->MRABNlevels,sizeof(int*));
  mesh->MRABpmlHaloIds = (int **) calloc(mesh->MRABNlevels, sizeof(int*));

  //count the pml elements
  mesh->pmlNelements=0;
  for (int lev =0;lev<mesh->MRABNlevels;lev++){
    for (int m=0;m<mesh->MRABNelements[lev];m++) {
      int e = mesh->MRABelementIds[lev][m];
      int type = mesh->elementInfo[e];
      if ((type==100)||(type==200)||(type==300)) {
        mesh->pmlNelements++;
        mesh->MRABpmlNelements[lev]++;
      }
    }
    for (int m=0;m<mesh->MRABNhaloElements[lev];m++) {
      int e = mesh->MRABhaloIds[lev][m];
      int type = mesh->elementInfo[e];
      if ((type==100)||(type==200)||(type==300))
        mesh->MRABpmlNhaloElements[lev]++;
    }
  }

  //set up the pml
  if (mesh->pmlNelements) {

    //construct a numbering of the pml elements
    int *pmlIds = (int *) calloc(mesh->Nelements,sizeof(int));
    int pmlcnt = 0;
    for (int e=0;e<mesh->Nelements;e++) {
      int type = mesh->elementInfo[e];
      if ((type==100)||(type==200)||(type==300))  //pml element
        pmlIds[e] = pmlcnt++;
    }

    //set up lists of pml elements and remove the pml elements from the nonpml MRAB lists
    for (int lev =0;lev<mesh->MRABNlevels;lev++){
      mesh->MRABpmlElementIds[lev] = (int *) calloc(mesh->MRABpmlNelements[lev],sizeof(int));
      mesh->MRABpmlIds[lev] = (int *) calloc(mesh->MRABpmlNelements[lev],sizeof(int));
      mesh->MRABpmlHaloElementIds[lev] = (int *) calloc(mesh->MRABpmlNhaloElements[lev],sizeof(int));
      mesh->MRABpmlHaloIds[lev] = (int *) calloc(mesh->MRABpmlNhaloElements[lev],sizeof(int));

      int pmlcnt = 0;
      int nonpmlcnt = 0;
      for (int m=0;m<mesh->MRABNelements[lev];m++){
        int e = mesh->MRABelementIds[lev][m];
        int type = mesh->elementInfo[e];

        if ((type==100)||(type==200)||(type==300)) { //pml element
          mesh->MRABpmlElementIds[lev][pmlcnt] = e;
          mesh->MRABpmlIds[lev][pmlcnt] = pmlIds[e];
          pmlcnt++;
        } else { //nonpml element
          mesh->MRABelementIds[lev][nonpmlcnt] = e;
          nonpmlcnt++;
        }
      }

      pmlcnt = 0;
      nonpmlcnt = 0;
      for (int m=0;m<mesh->MRABNhaloElements[lev];m++){
        int e = mesh->MRABhaloIds[lev][m];
        int type = mesh->elementInfo[e];

        if ((type==100)||(type==200)||(type==300)) { //pml element
          mesh->MRABpmlHaloElementIds[lev][pmlcnt] = e;
          mesh->MRABpmlHaloIds[lev][pmlcnt] = pmlIds[e];
          pmlcnt++;
        } else { //nonpml element
          mesh->MRABhaloIds[lev][nonpmlcnt] = e;
          nonpmlcnt++;
        }
      }

      //resize nonpml element lists
      mesh->MRABNelements[lev] -= mesh->MRABpmlNelements[lev];
      mesh->MRABNhaloElements[lev] -= mesh->MRABpmlNhaloElements[lev];
      mesh->MRABelementIds[lev] = (int*) realloc(mesh->MRABelementIds[lev],mesh->MRABNelements[lev]*sizeof(int));
      mesh->MRABhaloIds[lev]    = (int*) realloc(mesh->MRABhaloIds[lev],mesh->MRABNhaloElements[lev]*sizeof(int));
    }

    //set up damping parameter
    mesh->pmlSigmaX = (dfloat *) calloc(mesh->pmlNelements*mesh->cubNp,sizeof(dfloat));
    mesh->pmlSigmaY = (dfloat *) calloc(mesh->pmlNelements*mesh->cubNp,sizeof(dfloat));

    //find the bounding box of the whole domain and interior domain
    dfloat xmin = 1e9, xmax =-1e9;
    dfloat ymin = 1e9, ymax =-1e9;
    dfloat pmlxmin = 1e9, pmlxmax =-1e9;
    dfloat pmlymin = 1e9, pmlymax =-1e9;
    for (int e=0;e<mesh->Nelements;e++) {
      for (int n=0;n<mesh->Nverts;n++) {
        dfloat x = mesh->EX[e*mesh->Nverts+n];
        dfloat y = mesh->EY[e*mesh->Nverts+n];

        pmlxmin = (pmlxmin > x) ? x : pmlxmin;
        pmlymin = (pmlymin > y) ? y : pmlymin;
        pmlxmax = (pmlxmax < x) ? x : pmlxmax;
        pmlymax = (pmlymax < y) ? y : pmlymax;
      }

      //skip pml elements
      int type = mesh->elementInfo[e];
      if ((type==100)||(type==200)||(type==300)) continue;

      for (int n=0;n<mesh->Nverts;n++) {
        dfloat x = mesh->EX[e*mesh->Nverts+n];
        dfloat y = mesh->EY[e*mesh->Nverts+n];

        xmin = (xmin > x) ? x : xmin;
        ymin = (ymin > y) ? y : ymin;
        xmax = (xmax < x) ? x : xmax;
        ymax = (ymax < y) ? y : ymax;
      }
    }

    dfloat xmaxScale = pow(pmlxmax-xmax,2);
    dfloat xminScale = pow(pmlxmin-xmin,2);
    dfloat ymaxScale = pow(pmlymax-ymax,2);
    dfloat yminScale = pow(pmlymin-ymin,2);

    //set up the damping factor
    for (int lev =0;lev<mesh->MRABNlevels;lev++){
      for (int m=0;m<mesh->MRABpmlNelements[lev];m++) {
        int e = mesh->MRABpmlElementIds[lev][m];
        int pmlId = mesh->MRABpmlIds[lev][m];
        int type = mesh->elementInfo[e];

        int id = e*mesh->Nverts;

        dfloat xe1 = mesh->EX[id+0]; /* x-coordinates of vertices */
        dfloat xe2 = mesh->EX[id+1];
        dfloat xe3 = mesh->EX[id+2];

        dfloat ye1 = mesh->EY[id+0]; /* y-coordinates of vertices */
        dfloat ye2 = mesh->EY[id+1];
        dfloat ye3 = mesh->EY[id+2];

        for(int n=0;n<mesh->cubNp;++n){ /* for each node */
          // cubature node coordinates
          dfloat rn = mesh->cubr[n];
          dfloat sn = mesh->cubs[n];

          /* physical coordinate of interpolation node */
          dfloat x = -0.5*(rn+sn)*xe1 + 0.5*(1+rn)*xe2 + 0.5*(1+sn)*xe3;
          dfloat y = -0.5*(rn+sn)*ye1 + 0.5*(1+rn)*ye2 + 0.5*(1+sn)*ye3;

          if (type==100) { //X Pml
            if(x>xmax)
              mesh->pmlSigmaX[mesh->cubNp*pmlId + n] = xsigma*pow(x-xmax,2)/xmaxScale;
            if(x<xmin)
              mesh->pmlSigmaX[mesh->cubNp*pmlId + n] = xsigma*pow(x-xmin,2)/xminScale;
          } else if (type==200) { //Y Pml
            if(y>ymax)
              mesh->pmlSigmaY[mesh->cubNp*pmlId + n] = ysigma*pow(y-ymax,2)/ymaxScale;
            if(y<ymin)
              mesh->pmlSigmaY[mesh->cubNp*pmlId + n] = ysigma*pow(y-ymin,2)/yminScale;
          } else if (type==300) { //XY Pml
            if(x>xmax)
              mesh->pmlSigmaX[mesh->cubNp*pmlId + n] = xsigma*pow(x-xmax,2)/xmaxScale;
            if(x<xmin)
              mesh->pmlSigmaX[mesh->cubNp*pmlId + n] = xsigma*pow(x-xmin,2)/xminScale;
            if(y>ymax)
              mesh->pmlSigmaY[mesh->cubNp*pmlId + n] = ysigma*pow(y-ymax,2)/ymaxScale;
            if(y<ymin)
              mesh->pmlSigmaY[mesh->cubNp*pmlId + n] = ysigma*pow(y-ymin,2)/yminScale;
          }
        }
      }
    }

    printf("PML: found %d elements inside absorbing layers and %d elements outside\n",
    mesh->pmlNelements, mesh->Nelements-mesh->pmlNelements);

    mesh->pmlNfields = 2;
    mesh->pmlq    = (dfloat*) calloc(mesh->pmlNelements*mesh->Np*mesh->pmlNfields, sizeof(dfloat));
    mesh->pmlrhsq = (dfloat*) calloc(3*mesh->pmlNelements*mesh->Np*mesh->pmlNfields, sizeof(dfloat));

    // set up PML on DEVICE
    mesh->o_pmlq      = mesh->device.malloc(mesh->pmlNelements*mesh->Np*mesh->pmlNfields*sizeof(dfloat), mesh->pmlq);
    mesh->o_pmlrhsq   = mesh->device.malloc(3*mesh->pmlNelements*mesh->Np*mesh->pmlNfields*sizeof(dfloat), mesh->pmlrhsq);
    mesh->o_pmlSigmaX = mesh->device.malloc(mesh->pmlNelements*mesh->cubNp*sizeof(dfloat),mesh->pmlSigmaX);
    mesh->o_pmlSigmaY = mesh->device.malloc(mesh->pmlNelements*mesh->cubNp*sizeof(dfloat),mesh->pmlSigmaY);

    mesh->o_MRABpmlElementIds     = (occa::memory *) malloc(mesh->MRABNlevels*sizeof(occa::memory));
    mesh->o_MRABpmlIds            = (occa::memory *) malloc(mesh->MRABNlevels*sizeof(occa::memory));
    mesh->o_MRABpmlHaloElementIds = (occa::memory *) malloc(mesh->MRABNlevels*sizeof(occa::memory));
    mesh->o_MRABpmlHaloIds        = (occa::memory *) malloc(mesh->MRABNlevels*sizeof(occa::memory));
    for (int lev=0;lev<mesh->MRABNlevels;lev++) {
      if (mesh->MRABpmlNelements[lev]) {
        mesh->o_MRABpmlElementIds[lev] = mesh->device.malloc(mesh->MRABpmlNelements[lev]*sizeof(int),
           mesh->MRABpmlElementIds[lev]);
        mesh->o_MRABpmlIds[lev] = mesh->device.malloc(mesh->MRABpmlNelements[lev]*sizeof(int),
           mesh->MRABpmlIds[lev]);
      }
      if (mesh->MRABpmlNhaloElements[lev]) {
        mesh->o_MRABpmlHaloElementIds[lev] = mesh->device.malloc(mesh->MRABpmlNhaloElements[lev]*sizeof(int),
           mesh->MRABpmlHaloElementIds[lev]);
        mesh->o_MRABpmlHaloIds[lev] = mesh->device.malloc(mesh->MRABpmlNhaloElements[lev]*sizeof(int),
           mesh->MRABpmlHaloIds[lev]);
      }
    }

    free(pmlIds);
  }
}
