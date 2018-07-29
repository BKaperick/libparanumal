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
#include "mesh2D.h"

void boundaryConditions2D(int bc, dfloat t, dfloat x, dfloat y,
      dfloat uM, dfloat vM, dfloat pM,
            dfloat *uP, dfloat *vP, dfloat *pP){
  if(1){//bc==1){
    *uP = -uM;  
    *vP = -vM;  
    *pP = pM; 
  }   
  if(0){ // (bc==2){  
    dfloat dx = 1.f/sqrt(2.f);
    dfloat dy = 1.f/sqrt(2.f);
    dfloat omega = 10.f*M_PI;
    dfloat wave = cos(omega*(t-(x*dx+y*dy))); 
    dfloat uI = dx*wave;
    dfloat vI = dy*wave;
    dfloat pI = wave; 
    *uP = -uM -2.f*uI;  
    *vP = -vM -2.f*vI;
    *pP = pM;   
  }
}


void acousticsSurface2Dbbdg(mesh2D *mesh, int lev, dfloat t){

  // temporary storage for flux terms
  dfloat *fluxu = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxv = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxp = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));

  dfloat *fluxu_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxv_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxp_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));

  // for all elements
  for(int et=0;et<mesh->MRABNelements[lev];++et){
    int e = mesh->MRABelementIds[lev][et];
    // for all face nodes of all elements
    for(int n=0;n<mesh->Nfp*mesh->Nfaces;++n){
      // find face that owns this node
      int face = n/mesh->Nfp;

      // load surface geofactors for this face
      int sid = mesh->Nsgeo*(e*mesh->Nfaces+face);
      dfloat nx = mesh->sgeo[sid+0];
      dfloat ny = mesh->sgeo[sid+1];
      dfloat sJ = mesh->sgeo[sid+2];
      dfloat invJ = mesh->sgeo[sid+3];

      int id = n + e*mesh->Nfaces*mesh->Nfp;
      int idM = id*mesh->Nfields;
      int idP = mesh->mapP[id]*mesh->Nfields;

      // load negative trace node values of q
      dfloat uM = mesh->fQM[idM+0];
      dfloat vM = mesh->fQM[idM+1];
      dfloat pM = mesh->fQM[idM+2];

      // load positive trace node values of q
      dfloat uP = mesh->fQP[idP+0]; 
      dfloat vP = mesh->fQP[idP+1];
      dfloat pP = mesh->fQP[idP+2];

      // find boundary type
      int boundaryType = mesh->EToB[e*mesh->Nfaces+face];
      if(boundaryType>0) {
        int idM = mesh->vmapM[id];
        boundaryConditions2D(boundaryType, t, mesh->x[idM], mesh->y[idM], 
                              uM, vM, pM, &uP, &vP, &pP);
      }

      // compute (q^* - q^-)
      dfloat duS = 0.5f*(uP-uM) + mesh->Lambda2*(-nx)*(pP-pM);
      dfloat dvS = 0.5f*(vP-vM) + mesh->Lambda2*(-ny)*(pP-pM);
      dfloat dpS = 0.5f*(pP-pM) + mesh->Lambda2*(-nx*(uP-uM)-ny*(vP-vM));

      // evaluate "flux" terms: (sJ/J)*(A*nx+B*ny)*(q^* - q^-)
      fluxu[n] = invJ*sJ*(-nx*dpS);
      fluxv[n] = invJ*sJ*(-ny*dpS);
      fluxp[n] = invJ*sJ*(-nx*duS-ny*dvS);
    }

    // apply L0 to fluxes. use fact that L0 = tridiagonal in 2D
    for(int n=0;n<mesh->Nfp*mesh->Nfaces;++n){
    
      int id = n % mesh->Nfp;  // warning: redundant reads
      dfloat L0val = mesh->L0vals[3*id+1]; 

      dfloat utmpflux = L0val * fluxu[n];
      dfloat vtmpflux = L0val * fluxv[n];
      dfloat ptmpflux = L0val * fluxp[n];

      if (id > 0){     
        utmpflux += mesh->L0vals[3*id]*fluxu[n-1]; // add previous term
        vtmpflux += mesh->L0vals[3*id]*fluxv[n-1]; // add previous term
        ptmpflux += mesh->L0vals[3*id]*fluxp[n-1]; // add previous term
      }
      if (id < mesh->Nfp-1){
        utmpflux += mesh->L0vals[3*id+2]*fluxu[n+1];// add next term
        vtmpflux += mesh->L0vals[3*id+2]*fluxv[n+1];// add next term
        ptmpflux += mesh->L0vals[3*id+2]*fluxp[n+1];// add next term
      }
      fluxu_copy[n] = utmpflux;
      fluxv_copy[n] = vtmpflux;
      fluxp_copy[n] = ptmpflux;
    }

    // apply lift reduction and accumulate RHS
    for(int n=0;n<mesh->Np;++n){
      int id = 3*mesh->Nfields*(mesh->Np*e + n) + mesh->Nfields*mesh->MRABshiftIndex[lev];

      // load RHS
      dfloat rhsqnu = mesh->rhsq[id+0];
      dfloat rhsqnv = mesh->rhsq[id+1];
      dfloat rhsqnp = mesh->rhsq[id+2];

      // rhs += LIFT*((sJ/J)*(A*nx+B*ny)*(q^* - q^-))
      for (int m = 0; m < mesh->max_EL_nnz; ++m){
        int idm = m + n*mesh->max_EL_nnz;
        dfloat ELval = mesh->ELvals[idm];
        int ELid = mesh->ELids[idm];
        rhsqnu += ELval * fluxu_copy[ELid];
        rhsqnv += ELval * fluxv_copy[ELid];
        rhsqnp += ELval * fluxp_copy[ELid];
      }
      
      // store incremented rhs
      mesh->rhsq[id+0] = rhsqnu;
      mesh->rhsq[id+1] = rhsqnv;
      mesh->rhsq[id+2] = rhsqnp;  
    }
  }

  // free temporary flux storage
  free(fluxu); free(fluxv); free(fluxp);
  free(fluxu_copy); free(fluxv_copy); free(fluxp_copy);
}
