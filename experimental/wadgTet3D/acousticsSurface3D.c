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
			  dfloat *uP, dfloat *vP, dfloat *wP, dfloat *pP){
  if(bc==1){
    *uP = -uM;	
    *vP = -vM;
    *wP = -wM;
    *pP = pM;	
  }		
  if(bc==2){	
    dfloat dx = 1.f/sqrt(2.f);
    dfloat dy = 1.f/sqrt(2.f);
    dfloat dz = 0;
    dfloat omega = 10.f*M_PI;
    dfloat wave = cos(omega*(time-(x*dx+y*dy+z*dz)));	
    dfloat uI = dx*wave;
    dfloat vI = dy*wave;
    dfloat wI = dz*wave;
    dfloat pI = wave;	
    *uP = -uM -2.f*uI;	
    *vP = -vM -2.f*vI;
    *wP = -wM -2.f*wI;	
    *pP = pM;		
  }
}



// function to compute surface contributions 
// for nodal DG acoustics right hand side
void acousticsSurface3D(mesh3D *mesh, dfloat time){

  // temporary storage for flux terms
  dfloat *fluxu = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxv = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxw = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));
  dfloat *fluxp = (dfloat*) calloc(mesh->Nfp*mesh->Nfaces,sizeof(dfloat));

  // for all elements
  for(int e=0;e<mesh->Nelements;++e){
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
      int idM = mesh->Nfields*mesh->vmapM[id];
      int idP = mesh->Nfields*mesh->vmapP[id];

      if(idP<0) idP = idM;
      
      // load negative trace node values of q
      dfloat uM = mesh->q[idM+0];
      dfloat vM = mesh->q[idM+1];
      dfloat wM = mesh->q[idM+2];
      dfloat pM = mesh->q[idM+3];

      // load positive trace node values of q
      dfloat uP = mesh->q[idP+0]; 
      dfloat vP = mesh->q[idP+1];
      dfloat wP = mesh->q[idP+2];
      dfloat pP = mesh->q[idP+3];

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
      fluxp[n] = invJ*sJ*(-nx*duS-ny*dvS-nz*dwS);
    }

    // for each node in the element 
    for(int n=0;n<mesh->Np;++n){
      int id = mesh->Nfields*(mesh->Np*e + n);

      // load rhs data from volume fluxes
      dfloat rhsu = mesh->rhsq[id];
      dfloat rhsv = mesh->rhsq[id+1];
      dfloat rhsw = mesh->rhsq[id+2];
      dfloat rhsp = mesh->rhsq[id+3];
      
      // rhs += LIFT*((sJ/J)*(A*nx+B*ny)*(q^* - q^-))
      for(int m=0;m<(mesh->Nfp*mesh->Nfaces);++m){
	dfloat L = mesh->LIFT[n*mesh->Nfp*mesh->Nfaces+m];
	rhsu += L*fluxu[m];
	rhsv += L*fluxv[m];
	rhsw += L*fluxw[m];
	rhsp += L*fluxp[m];
      }

      // store incremented rhs
      mesh->rhsq[id]   = rhsu;
      mesh->rhsq[id+1] = rhsv;
      mesh->rhsq[id+2] = rhsw;
      mesh->rhsq[id+3] = rhsp;

    }
  }

  // free temporary flux storage
  free(fluxu); free(fluxv); free(fluxw); free(fluxp);
}
    
