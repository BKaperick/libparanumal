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

#include <stdio.h>
#include <stdlib.h>
#include "mesh3D.h"
#include "mpi.h"

dfloat wavespeed(dfloat x, dfloat y, dfloat z);

void meshSetPolynomialDegree3D(mesh3D *mesh, int NMax) {

  dfloat omega = 300.; //frequency range to be 'well resolved'

  mesh->N = (int *) calloc(mesh->Nelements+mesh->totalHaloPairs,sizeof(int));

  for(int e=0;e<mesh->Nelements;++e){ 
    //find max length scale
    dfloat hmax = 0.;  //element max
    for(int f=0;f<mesh->Nfaces;++f){
      int sid = mesh->Nsgeo*(mesh->Nfaces*e + f);
      dfloat sJ   = mesh->sgeo[sid + SJID];
      dfloat invJ = mesh->sgeo[sid + IJID];

      // sJ = L/2, J = A/2,   sJ/J = L/A = L/(0.5*h*L) = 2/h
      // h = 0.5/(sJ/J)

      dfloat hest = .5/(sJ*invJ);
      hmax = mymax(hmax, hest);
    }

    //find the minimun wavespeed
    int id = e*mesh->Nverts+0;
    
    dfloat xe1 = mesh->EX[id+0]; /* x-coordinates of vertices */
    dfloat xe2 = mesh->EX[id+1];
    dfloat xe3 = mesh->EX[id+2];
    dfloat xe4 = mesh->EX[id+3];
    
    dfloat ye1 = mesh->EY[id+0]; /* y-coordinates of vertices */
    dfloat ye2 = mesh->EY[id+1];
    dfloat ye3 = mesh->EY[id+2];
    dfloat ye4 = mesh->EY[id+3];

    dfloat ze1 = mesh->EZ[id+0]; /* y-coordinates of vertices */
    dfloat ze2 = mesh->EZ[id+1];
    dfloat ze3 = mesh->EZ[id+2];
    dfloat ze4 = mesh->EZ[id+3];
    
    dfloat c2Min = 1e9;
    for(int n=0;n<mesh->cubNp[NMax];++n){ /* for each node */
      // cubature node coordinates
      dfloat rn = mesh->cubr[NMax][n]; 
      dfloat sn = mesh->cubs[NMax][n];
      dfloat tn = mesh->cubt[NMax][n];

      /* physical coordinate of interpolation node */
      dfloat x = -0.5*(rn+sn+tn+1.)*xe1 + 0.5*(1+rn)*xe2 + 0.5*(1+sn)*xe3 + 0.5*(1+tn)*xe4 ;
      dfloat y = -0.5*(rn+sn+tn+1.)*ye1 + 0.5*(1+rn)*ye2 + 0.5*(1+sn)*ye3 + 0.5*(1+tn)*ye4 ;
      dfloat z = -0.5*(rn+sn+tn+1.)*ze1 + 0.5*(1+rn)*ze2 + 0.5*(1+sn)*ze3 + 0.5*(1+tn)*ze4 ;
      
      dfloat c2 = wavespeed(x,y,z);
      #if WADG
        c2Min = mymin(c2,c2Min);
      #else
        c2Min = 1.;
      #endif
    }

    //set degree of approximation
    // 2p+1 > omega*h/c
    mesh->N[e] = mymin(mymax(ceil(0.5*(omega*hmax)/sqrt(c2Min) - 1),1),NMax);
  }

  int *sendBuffer;
  if (mesh->totalHaloPairs) 
    sendBuffer = (int *) calloc(mesh->totalHaloPairs,sizeof(int));

  //shift polynomial orders so that no element is beside an element of 2 degree higher/lower
  for (int p=NMax-2; p > 0; p--){
    if (mesh->totalHaloPairs) 
      meshHaloExchange(mesh, sizeof(int), mesh->N, sendBuffer, mesh->N+mesh->Nelements);
    for (int e =0; e<mesh->Nelements;e++) {
      if (mesh->N[e] == p) { //find elements of degree p 
        for (int f=0;f<mesh->Nfaces;f++) { //check for a degree > p+1 neighbour
          int eP = mesh->EToE[mesh->Nfaces*e+f];
          if (eP > -1) 
            if (mesh->N[eP] > p+1)
              mesh->N[e] = p+1;  //if one exists, raise the degree of the current element
        }
      }
    }
  }
  if (mesh->totalHaloPairs) {
    meshHaloExchange(mesh, sizeof(int), mesh->N, sendBuffer, mesh->N+mesh->Nelements);
    free(sendBuffer);
  }

  int N = 0;
  int *Ncnt = (int *) calloc(NMax+1,sizeof(int));
  NMax = 0;
  for (int e =0; e<mesh->Nelements;e++) {
    Ncnt[mesh->N[e]]++;
    N = mymax(N,mesh->N[e]);
  }
  MPI_Allreduce(&N, &NMax, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
  

  mesh->NMax = NMax;
  mesh->NpMax = mesh->Np[NMax];
  mesh->NfpMax = mesh->Nfp[NMax];  
  mesh->cubNpMax = mesh->cubNp[NMax];  

  printf("Max degrees and sizes: N = %d, Np = %d\n", mesh->NMax, mesh->NpMax);
  for (int p =1;p<=NMax;p++) printf("   %d elements of degree %d \n", Ncnt[p], p);
  free(Ncnt);
}
