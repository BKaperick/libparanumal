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

#include "acoustics3D.h"

void boundaryConditions3D(int bc, dfloat time, dfloat x, dfloat y, dfloat z,
        dfloat uM, dfloat vM, dfloat wM, dfloat pM,
        dfloat *uP, dfloat *vP, dfloat *wP, dfloat *pP);


void acousticsPmlSurface3Dbbdg(mesh3D *mesh, int lev, dfloat time){

  // temporary storage for flux terms
  dfloat *fluxu = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxv = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxw = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxpx = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxpy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxpz = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));

  dfloat *fluxu_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxv_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxw_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxpx_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxpy_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxpz_copy = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));

  // for all elements
  for(int et=0;et<mesh->MRABpmlNelements[lev];++et){
    int e = mesh->MRABpmlElementIds[lev][et];
    int pmlId = mesh->MRABpmlIds[lev][et];
    // for all face nodes of all elements
    for(int n=0;n<mesh->Nfp*mesh->Nfaces;++n){
      // find face that owns this node
      int face = n/mesh->Nfp;

      // load surface geofactors for this face
      int  sid = mesh->Nsgeo*(e*mesh->Nfaces+face);
      dfloat nx = mesh->sgeo[sid+NXID];
      dfloat ny = mesh->sgeo[sid+NYID];
      dfloat nz = mesh->sgeo[sid+NZID];
      dfloat sJ = mesh->sgeo[sid+SJID];
      dfloat invJ = mesh->sgeo[sid+IJID];

      // indices of negative and positive traces of face node
      int id  = e*mesh->Nfp*mesh->Nfaces + n;
      int idM = id*mesh->Nfields;
      int idP = mesh->mapP[id]*mesh->Nfields;

      // load negative trace node values of q
      dfloat uM = mesh->fQM[idM+0];
      dfloat vM = mesh->fQM[idM+1];
      dfloat wM = mesh->fQM[idM+2];
      dfloat pM = mesh->fQM[idM+3];

      // load positive trace node values of q
      dfloat uP = mesh->fQP[idP+0];
      dfloat vP = mesh->fQP[idP+1];
      dfloat wP = mesh->fQP[idP+2];
      dfloat pP = mesh->fQP[idP+3];

      // find boundary type
      int boundaryType = mesh->EToB[e*mesh->Nfaces+face];
      if(boundaryType>0)
        boundaryConditions3D(boundaryType, time,
                 mesh->x[idM], mesh->y[idM], mesh->z[idM],
                 uM, vM, wM, pM,
                 &uP, &vP,&wP, &pP);

      // compute (q^* - q^-)
      dfloat duS = 0.5*(uP-uM) + mesh->Lambda2*(-nx*(pP-pM));
      dfloat dvS = 0.5*(vP-vM) + mesh->Lambda2*(-ny*(pP-pM));
      dfloat dwS = 0.5*(wP-wM) + mesh->Lambda2*(-nz*(pP-pM));
      dfloat dpS = 0.5*(pP-pM) + mesh->Lambda2*(-nx*(uP-uM)-ny*(vP-vM)-nz*(wP-wM));

      // evaluate "flux" terms: (sJ/J)*(A*nx+B*ny)*(q^* - q^-)
      fluxu[n] = invJ*sJ*(-nx*dpS);
      fluxv[n] = invJ*sJ*(-ny*dpS);
      fluxw[n] = invJ*sJ*(-nz*dpS);
      fluxpx[n] = invJ*sJ*(-nx*duS);
      fluxpy[n] = invJ*sJ*(-ny*dvS);
      fluxpz[n] = invJ*sJ*(-nz*dwS);
    }

    // apply L0 to fluxes.
    for(int n=0;n<mesh->Nfp*mesh->Nfaces;++n){

      int id = n%mesh->Nfp;
      int f  = n/mesh->Nfp;

      dfloat utmpflux = 0.0;
      dfloat vtmpflux = 0.0;
      dfloat wtmpflux = 0.0;
      dfloat pxtmpflux = 0.0;
      dfloat pytmpflux = 0.0;
      dfloat pztmpflux = 0.0;

      // sparse application of L0
      for (int m = 0; m < 7; ++m){
        int   L0id  = mesh->L0ids [7*id+m];
        dfloat L0val = mesh->L0vals[7*id+m];

        utmpflux += L0val * fluxu[L0id+f*mesh->Nfp];
        vtmpflux += L0val * fluxv[L0id+f*mesh->Nfp];
        wtmpflux += L0val * fluxw[L0id+f*mesh->Nfp];
        pxtmpflux += L0val * fluxpx[L0id+f*mesh->Nfp];
        pytmpflux += L0val * fluxpy[L0id+f*mesh->Nfp];
        pztmpflux += L0val * fluxpz[L0id+f*mesh->Nfp];
      }

      fluxu_copy[n] = utmpflux;
      fluxv_copy[n] = vtmpflux;
      fluxw_copy[n] = wtmpflux;
      fluxpx_copy[n] = pxtmpflux;
      fluxpy_copy[n] = pytmpflux;
      fluxpz_copy[n] = pztmpflux;
    }

    // apply lift reduction and accumulate RHS
    for(int n=0;n<mesh->Np;++n){
      int id = 3*mesh->Nfields*(mesh->Np*e + n) + mesh->Nfields*mesh->MRABshiftIndex[lev];
      int pmlrhsId = 3*mesh->pmlNfields*(mesh->Np*pmlId + n) + mesh->pmlNfields*mesh->MRABshiftIndex[lev];

      // load RHS
      dfloat rhsqnu = mesh->rhsq[id+0];
      dfloat rhsqnv = mesh->rhsq[id+1];
      dfloat rhsqnw = mesh->rhsq[id+2];
      dfloat rhsqnpx = mesh->pmlrhsq[pmlrhsId+0];
      dfloat rhsqnpy = mesh->pmlrhsq[pmlrhsId+1];
      dfloat rhsqnpz = mesh->pmlrhsq[pmlrhsId+2];

      // sparse application of EL
      for (int m = 0; m < mesh->max_EL_nnz; ++m){
        int id = m + n*mesh->max_EL_nnz;
        dfloat ELval = mesh->ELvals[id];
        int   ELid  = mesh->ELids [id];

        rhsqnu += ELval * fluxu_copy[ELid];
        rhsqnv += ELval * fluxv_copy[ELid];
        rhsqnw += ELval * fluxw_copy[ELid];
        rhsqnpx += ELval * fluxpx_copy[ELid];
        rhsqnpy += ELval * fluxpy_copy[ELid];
        rhsqnpz += ELval * fluxpz_copy[ELid];
      }

      // store incremented rhs
      mesh->rhsq[id+0] = rhsqnu;
      mesh->rhsq[id+1] = rhsqnv;
      mesh->rhsq[id+2] = rhsqnw;
      mesh->pmlrhsq[pmlrhsId+0] = rhsqnpx;
      mesh->pmlrhsq[pmlrhsId+1] = rhsqnpy;
      mesh->pmlrhsq[pmlrhsId+2] = rhsqnpz;

    }
  }

  // free temporary flux storage
  free(fluxu); free(fluxv); free(fluxw);
  free(fluxpx); free(fluxpy); free(fluxpz);
  free(fluxu_copy); free(fluxv_copy); free(fluxw_copy);
  free(fluxpx_copy); free(fluxpy_copy); free(fluxpz_copy);
}
