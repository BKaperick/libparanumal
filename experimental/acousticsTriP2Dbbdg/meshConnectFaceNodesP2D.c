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

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include "mesh2D.h"

int findBestMatch(dfloat x1, dfloat y1, 
		   int Np2, int *nodeList, dfloat *x2, dfloat *y2, int *nP){
  
  int matchIndex = nodeList[0];
  dfloat mindist2 = pow(x1-x2[nodeList[0]],2) + pow(y1-y2[nodeList[0]],2);

  for(int n=1;n<Np2;++n){
    
    /* next node */
    const int i2 = nodeList[n];

    /* distance between target and next node */
    const dfloat dist2 = pow(x1-x2[i2],2) + pow(y1-y2[i2],2);
    
    /* if next node is closer to target update match */
    if(dist2<mindist2){
      mindist2 = dist2;
      matchIndex = i2;
      *nP = n;
    }
  }
  if(mindist2>1e-3) printf("arggh - bad match: x,y=%g,%g\n", x1,y1);
  return matchIndex;
}
		

// serial face-node to face-node connection
void meshConnectFaceNodesP2D(mesh2D *mesh){
  
  /* volume indices of the interior and exterior face nodes for each element */
  mesh->vmapM = (int*) calloc(mesh->NfpMax*mesh->Nfaces*mesh->Nelements, sizeof(int));
  mesh->vmapP = (int*) calloc(mesh->NfpMax*mesh->Nfaces*mesh->Nelements, sizeof(int));
  mesh->mapP  = (int*) calloc(mesh->NfpMax*mesh->Nfaces*mesh->Nelements, sizeof(int));
  
  dfloat xConnect[mesh->NpMax];
  dfloat yConnect[mesh->NpMax];

  /* assume elements already connected */
  for(int e=0;e<mesh->Nelements;++e){
    int N = mesh->N[e];
    for(int f=0;f<mesh->Nfaces;++f){
      int eP = mesh->EToE[e*mesh->Nfaces+f];
      int fP = mesh->EToF[e*mesh->Nfaces+f];
 
      if(eP<0 || fP<0){ // fake connections for unconnected faces
         eP = e;
         fP = f;
      }
      int NP = mesh->N[eP];

      /* for each node on this face find the neighbor node */
      for(int n=0;n<mesh->Nfp[N];++n){
        int   id = e*mesh->Nfaces*mesh->NfpMax + f*mesh->NfpMax + n;     //face node index
      	int  idM = e*mesh->NpMax + mesh->faceNodes[N][f*mesh->Nfp[N]+n]; //global index
      	mesh->vmapM[id] = idM;
      }
    
      //Construct node ordering of neighbour which agrees with local element's degree
      int id = eP*mesh->Nverts;

      dfloat xe1 = mesh->EX[id+0]; /* x-coordinates of vertices */
      dfloat xe2 = mesh->EX[id+1];
      dfloat xe3 = mesh->EX[id+2];

      dfloat ye1 = mesh->EY[id+0]; /* y-coordinates of vertices */
      dfloat ye2 = mesh->EY[id+1];
      dfloat ye3 = mesh->EY[id+2];
      
      for(int n=0;n<mesh->Np[N];++n){ /* for each node */
        
        /* (r,s,t) coordinates of interpolation nodes*/
        dfloat rn = mesh->r[N][n]; 
        dfloat sn = mesh->s[N][n];

        /* physical coordinate of interpolation node */
        xConnect[n] = -0.5*(rn+sn)*xe1 + 0.5*(1+rn)*xe2 + 0.5*(1+sn)*xe3;
        yConnect[n] = -0.5*(rn+sn)*ye1 + 0.5*(1+rn)*ye2 + 0.5*(1+sn)*ye3;
      }

      for(int n=0;n<mesh->Nfp[N];++n){
        int  idM = mesh->faceNodes[N][f*mesh->Nfp[N]+n]; 
        dfloat xM = mesh->x[e*mesh->NpMax+idM];
        dfloat yM = mesh->y[e*mesh->NpMax+idM];
        int nP = 0;
        
        int  idP = findBestMatch(xM, yM,
                mesh->Nfp[N], 
                mesh->faceNodes[N]+fP*mesh->Nfp[N],
                xConnect,
                yConnect, &nP);

        int   id = mesh->Nfaces*mesh->NfpMax*e + f*mesh->NfpMax + n;
        mesh->vmapP[id] = idP + eP*mesh->NpMax;
        mesh->mapP[id] = eP*mesh->Nfaces*mesh->NfpMax + fP*mesh->NfpMax + nP;
      }
    }
  }
}
