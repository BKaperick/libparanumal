#include "acoustics3D.h"

void acousticsRickerPulse3D(dfloat x, dfloat y, dfloat z, dfloat t, dfloat f, dfloat c,
                           dfloat *u, dfloat *v, dfloat *w, dfloat *p);

void acousticsMRABUpdate3D(mesh3D *mesh,  
                           dfloat a1,
                           dfloat a2,
                           dfloat a3, iint lev, dfloat t, dfloat dt){

  dfloat *un = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *unp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *sourceq = (dfloat *) calloc(mesh->Nfields*mesh->NfpMax,sizeof(dfloat));

  for(iint et=0;et<mesh->MRABNelements[lev];et++){
    iint e = mesh->MRABelementIds[lev][et];
    iint N = mesh->N[e];

    for(iint n=0;n<mesh->Np[N];++n){
      iint id = mesh->Nfields*(e*mesh->NpMax + n);

      iint rhsId1 = 3*id + ((mesh->MRABshiftIndex[lev]+0)%3)*mesh->Nfields;
      iint rhsId2 = 3*id + ((mesh->MRABshiftIndex[lev]+1)%3)*mesh->Nfields;
      iint rhsId3 = 3*id + ((mesh->MRABshiftIndex[lev]+2)%3)*mesh->Nfields;
      for (iint fld=0;fld<mesh->Nfields;fld++)
        mesh->q[id+fld] += dt*(a1*mesh->rhsq[rhsId1+fld] + a2*mesh->rhsq[rhsId2+fld] + a3*mesh->rhsq[rhsId3+fld]);
      
    }

    //project traces to proper order for neighbour
    for (iint f =0;f<mesh->Nfaces;f++) {
      //load local traces
      for (iint n=0;n<mesh->Nfp[N];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qidM = mesh->Nfields*mesh->vmapM[id];
        iint qid = mesh->Nfields*id;

        un[n] = mesh->q[qidM+0];
        vn[n] = mesh->q[qidM+1];
        wn[n] = mesh->q[qidM+2];
        pn[n] = mesh->q[qidM+3];

        mesh->fQM[qid+0] = un[n];
        mesh->fQM[qid+1] = vn[n];
        mesh->fQM[qid+2] = wn[n];
        mesh->fQM[qid+3] = pn[n];
      }

      //check if this face is an interface of the source injection region
      iint bc = mesh->EToB[e*mesh->Nfaces+f];
      if ((bc==-10)||(bc==-11)) {
        for (iint n=0;n<mesh->Nfp[N];n++) {
          iint id = n + f*mesh->NfpMax + e*mesh->Nfaces*mesh->NfpMax;
          iint idM = mesh->vmapM[id];

          //get the nodal values of the incident field along the trace
          dfloat x = mesh->x[idM];
          dfloat y = mesh->y[idM];
          dfloat z = mesh->z[idM];

          dfloat x0 = mesh->sourceX0;
          dfloat y0 = mesh->sourceY0;
          dfloat z0 = mesh->sourceZ0;
          dfloat t0 = mesh->sourceT0;
          dfloat freq = mesh->sourceFreq;

          dfloat c = sqrt(mesh->sourceC2);

          int qid = mesh->Nfields*n;
          acousticsRickerPulse3D(x-x0, y-y0, z-z0, t+t0, freq,c, sourceq+qid+0, sourceq+qid+1, sourceq+qid+2, sourceq+qid+3);
        }

        dfloat s = 0.f;
        if (bc==-10) s= 1.f;
        if (bc==-11) s=-1.f;

        //Transform incident field trace to BB modal space and add into positive trace
        for (iint n=0; n<mesh->Nfp[N]; n++){
          dfloat sourceu = 0.0;
          dfloat sourcev = 0.0;
          dfloat sourcew = 0.0;
          dfloat sourcep = 0.0;
          for (iint m=0; m<mesh->Nfp[N]; m++){
            sourceu += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+0];
            sourcev += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+1];
            sourcew += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+2];
            sourcep += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+3];
          }
          //adjust positive trace values with the incident field
          iint id  = e*mesh->Nfaces*mesh->NfpMax + f*mesh->NfpMax + n;
          iint qidM = mesh->Nfields*mesh->vmapM[id];

          iint qid = mesh->Nfields*id;
          un[n] += s*sourceu;
          vn[n] += s*sourcev;
          wn[n] += s*sourcew;
          pn[n] += s*sourcep;
        }
      } 
      
      // load element neighbour
      iint eP = mesh->EToE[e*mesh->Nfaces+f];
      if (eP<0) eP = e; //boundary
      iint NP = mesh->N[eP]; 

      if (NP > N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<3;m++){ //apply raise operator sparsly
            dfloat BBRaiseVal = mesh->BBRaiseVals[N][3*n+m];
            iint BBRaiseid = mesh->BBRaiseids[N][3*n+m];
            unp[n] += BBRaiseVal*un[BBRaiseid];
            vnp[n] += BBRaiseVal*vn[BBRaiseid];
            wnp[n] += BBRaiseVal*wn[BBRaiseid];
            pnp[n] += BBRaiseVal*pn[BBRaiseid];
          }
        }
      } else if (NP < N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<mesh->Nfp[N];m++){
            iint id = n*mesh->Nfp[N] + m;
            unp[n] += mesh->BBLower[N][id]*un[m];
            vnp[n] += mesh->BBLower[N][id]*vn[m];
            wnp[n] += mesh->BBLower[N][id]*wn[m];
            pnp[n] += mesh->BBLower[N][id]*pn[m];
          }
        }
      } else { //equal order neighbor
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = un[n];
          vnp[n] = vn[n];
          wnp[n] = wn[n];
          pnp[n] = pn[n];
        }
      }

      //write new traces to fQ
      for (iint n=0;n<mesh->Nfp[NP];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qid = mesh->Nfields*id;

        mesh->fQP[qid+0] = unp[n];
        mesh->fQP[qid+1] = vnp[n];
        mesh->fQP[qid+2] = wnp[n];
        mesh->fQP[qid+3] = pnp[n];
      }
    }
  }

  //rotate index
  mesh->MRABshiftIndex[lev] = (mesh->MRABshiftIndex[lev]+1)%3;

  free(un); free(vn); free(wn); free(pn);
  free(unp); free(vnp); free(wnp); free(pnp);
  free(sourceq);
}

void acousticsMRABUpdateTrace3D(mesh3D *mesh,  
                           dfloat a1,
                           dfloat a2,
                           dfloat a3, iint lev, dfloat t, dfloat dt){

  dfloat *un = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *unp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *sourceq = (dfloat *) calloc(mesh->Nfields*mesh->NfpMax,sizeof(dfloat));

  dfloat *s_q = (dfloat*) calloc(mesh->NpMax*mesh->Nfields,sizeof(dfloat));

  for(iint et=0;et<mesh->MRABNelements[lev];et++){
    iint e = mesh->MRABelementIds[lev][et];
    iint N = mesh->N[e];

    for(iint n=0;n<mesh->Np[N];++n){
      iint id = mesh->Nfields*(e*mesh->NpMax + n);

      iint rhsId1 = 3*id + ((mesh->MRABshiftIndex[lev]+0)%3)*mesh->Nfields;
      iint rhsId2 = 3*id + ((mesh->MRABshiftIndex[lev]+1)%3)*mesh->Nfields;
      iint rhsId3 = 3*id + ((mesh->MRABshiftIndex[lev]+2)%3)*mesh->Nfields;

      // Increment solutions
      for (iint fld=0; fld < mesh->Nfields; ++fld){ 
        s_q[n*mesh->Nfields+fld] = mesh->q[id+fld] + dt*(a1*mesh->rhsq[rhsId1+fld] + a2*mesh->rhsq[rhsId2+fld] + a3*mesh->rhsq[rhsId3+fld]);
      }      
    }

    //project traces to proper order for neighbour
    for (iint f =0;f<mesh->Nfaces;f++) {
      //load local traces
      for (iint n=0;n<mesh->Nfp[N];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qidM = mesh->Nfields*(mesh->vmapM[id]-e*mesh->NpMax);
        iint qid = mesh->Nfields*id;

        un[n] = s_q[qidM+0];
        vn[n] = s_q[qidM+1];
        wn[n] = s_q[qidM+2];
        pn[n] = s_q[qidM+3];

        mesh->fQM[qid+0] = un[n];
        mesh->fQM[qid+1] = vn[n];
        mesh->fQM[qid+2] = wn[n];
        mesh->fQM[qid+3] = pn[n];
      }

      //check if this face is an interface of the source injection region
      iint bc = mesh->EToB[e*mesh->Nfaces+f];
      if ((bc==-10)||(bc==-11)) {
        for (iint n=0;n<mesh->Nfp[N];n++) {
          iint id = n + f*mesh->NfpMax + e*mesh->Nfaces*mesh->NfpMax;
          iint idM = mesh->vmapM[id];

          //get the nodal values of the incident field along the trace
          dfloat x = mesh->x[idM];
          dfloat y = mesh->y[idM];
          dfloat z = mesh->z[idM];

          dfloat x0 = mesh->sourceX0;
          dfloat y0 = mesh->sourceY0;
          dfloat z0 = mesh->sourceZ0;
          dfloat t0 = mesh->sourceT0;
          dfloat freq = mesh->sourceFreq;

          dfloat c = sqrt(mesh->sourceC2);

          int qid = mesh->Nfields*n;
          acousticsRickerPulse3D(x-x0, y-y0, z-z0, t+t0, freq,c, sourceq+qid+0, sourceq+qid+1, sourceq+qid+2, sourceq+qid+3);
        }

        dfloat s = 0.f;
        if (bc==-10) s= 1.f;
        if (bc==-11) s=-1.f;

        //Transform incident field trace to BB modal space and add into positive trace
        for (iint n=0; n<mesh->Nfp[N]; n++){
          dfloat sourceu = 0.0;
          dfloat sourcev = 0.0;
          dfloat sourcew = 0.0;
          dfloat sourcep = 0.0;
          for (iint m=0; m<mesh->Nfp[N]; m++){
            sourceu += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+0];
            sourcev += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+1];
            sourcew += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+2];
            sourcep += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+3];
          }
          //adjust positive trace values with the incident field
          iint id  = e*mesh->Nfaces*mesh->NfpMax + f*mesh->NfpMax + n;
          iint qidM = mesh->Nfields*mesh->vmapM[id];

          iint qid = mesh->Nfields*id;
          un[n] += s*sourceu;
          vn[n] += s*sourcev;
          wn[n] += s*sourcew;
          pn[n] += s*sourcep;
        }
      } 


      // load element neighbour
      iint eP = mesh->EToE[e*mesh->Nfaces+f];
      if (eP<0) eP = e; //boundary
      iint NP = mesh->N[eP]; 

      if (NP > N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<3;m++){ //apply raise operator sparsly
            dfloat BBRaiseVal = mesh->BBRaiseVals[N][3*n+m];
            iint BBRaiseid = mesh->BBRaiseids[N][3*n+m];
            unp[n] += BBRaiseVal*un[BBRaiseid];
            vnp[n] += BBRaiseVal*vn[BBRaiseid];
            wnp[n] += BBRaiseVal*wn[BBRaiseid];
            pnp[n] += BBRaiseVal*pn[BBRaiseid];
          }
        }
      } else if (NP < N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<mesh->Nfp[N];m++){
            iint id = n*mesh->Nfp[N] + m;
            unp[n] += mesh->BBLower[N][id]*un[m];
            vnp[n] += mesh->BBLower[N][id]*vn[m];
            wnp[n] += mesh->BBLower[N][id]*wn[m];
            pnp[n] += mesh->BBLower[N][id]*pn[m];
          }
        }
      } else { //equal order neighbor
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = un[n];
          vnp[n] = vn[n];
          wnp[n] = wn[n];
          pnp[n] = pn[n];
        }
      }

      //write new traces to fQ
      for (iint n=0;n<mesh->Nfp[NP];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qid = mesh->Nfields*id;

        mesh->fQP[qid+0] = unp[n];
        mesh->fQP[qid+1] = vnp[n];
        mesh->fQP[qid+2] = wnp[n];
        mesh->fQP[qid+3] = pnp[n];
      }
      for (iint n=0;n<mesh->Nfp[N];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qid = mesh->Nfields*id;

        mesh->fQM[qid+0] = un[n];
        mesh->fQM[qid+1] = vn[n];
        mesh->fQM[qid+2] = wn[n];
        mesh->fQM[qid+3] = pn[n];
      }
    }
  }

  free(un); free(vn); free(wn); free(pn);
  free(unp); free(vnp); free(wnp); free(pnp);
  free(s_q);
  free(sourceq);
}


void acousticsMRABUpdate3D_wadg(mesh3D *mesh,  
                           dfloat a1,
                           dfloat a2,
                           dfloat a3, iint lev, dfloat t, dfloat dt){
  
  dfloat *un = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *unp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *sourceq = (dfloat *) calloc(mesh->Nfields*mesh->NfpMax,sizeof(dfloat));
  dfloat *p = (dfloat*) calloc(mesh->cubNpMax,sizeof(dfloat));

  for(iint et=0;et<mesh->MRABNelements[lev];et++){
    iint e = mesh->MRABelementIds[lev][et];
    iint N = mesh->N[e];

    // Interpolate rhs to cubature nodes
    for(iint n=0;n<mesh->cubNp[N];++n){
      p[n] = 0.f;
      iint id = mesh->Nfields*(e*mesh->NpMax);
      iint rhsId = 3*id + mesh->MRABshiftIndex[lev]*mesh->Nfields;

      for (iint i=0;i<mesh->Np[N];++i){
        p[n] += mesh->cubInterp[N][n*mesh->Np[N] + i] * mesh->rhsq[rhsId + 3*mesh->Nfields*i + 3];
      }

      // Multiply result by wavespeed c2 at cubature node
      p[n] *= mesh->c2[n + e*mesh->cubNpMax];
    }

    // Increment solution, project result back down
    for(iint n=0;n<mesh->Np[N];++n){
      // Extract velocity rhs
      iint id = mesh->Nfields*(e*mesh->NpMax + n);
      iint rhsId = 3*id + mesh->MRABshiftIndex[lev]*mesh->Nfields;
      
      // Project scaled rhs down
      dfloat rhsp = 0.f;
      for (iint i=0;i<mesh->cubNp[N];++i){
        rhsp += mesh->cubProject[N][n*mesh->cubNp[N] + i] * p[i];
      }
      mesh->rhsq[rhsId+3] = rhsp;

      iint rhsId1 = 3*id + ((mesh->MRABshiftIndex[lev]+0)%3)*mesh->Nfields;
      iint rhsId2 = 3*id + ((mesh->MRABshiftIndex[lev]+1)%3)*mesh->Nfields;
      iint rhsId3 = 3*id + ((mesh->MRABshiftIndex[lev]+2)%3)*mesh->Nfields;
      for (iint fld=0;fld<mesh->Nfields;fld++)
        mesh->q[id+fld] += dt*(a1*mesh->rhsq[rhsId1+fld] + a2*mesh->rhsq[rhsId2+fld] + a3*mesh->rhsq[rhsId3+fld]);
    }


    //project traces to proper order for neighbour
    for (iint f=0;f<mesh->Nfaces;f++) {
      //load local traces
      for (iint n=0;n<mesh->Nfp[N];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qidM = mesh->Nfields*mesh->vmapM[id];
        iint qid = mesh->Nfields*id;

        un[n] = mesh->q[qidM+0];
        vn[n] = mesh->q[qidM+1];
        wn[n] = mesh->q[qidM+2];
        pn[n] = mesh->q[qidM+3];

        mesh->fQM[qid+0] = un[n];
        mesh->fQM[qid+1] = vn[n];
        mesh->fQM[qid+2] = wn[n];
        mesh->fQM[qid+3] = pn[n];
      }

      //check if this face is an interface of the source injection region
      iint bc = mesh->EToB[e*mesh->Nfaces+f];
      if ((bc==-10)||(bc==-11)) {
        for (iint n=0;n<mesh->Nfp[N];n++) {
          iint id = n + f*mesh->NfpMax + e*mesh->Nfaces*mesh->NfpMax;
          iint idM = mesh->vmapM[id];

          //get the nodal values of the incident field along the trace
          dfloat x = mesh->x[idM];
          dfloat y = mesh->y[idM];
          dfloat z = mesh->z[idM];

          dfloat x0 = mesh->sourceX0;
          dfloat y0 = mesh->sourceY0;
          dfloat z0 = mesh->sourceZ0;
          dfloat t0 = mesh->sourceT0;
          dfloat freq = mesh->sourceFreq;

          dfloat c = sqrt(mesh->sourceC2);

          int qid = mesh->Nfields*n;
          acousticsRickerPulse3D(x-x0, y-y0, z-z0, t+t0, freq,c, sourceq+qid+0, sourceq+qid+1, sourceq+qid+2, sourceq+qid+3);
        }

        dfloat s = 0.f;
        if (bc==-10) s= 1.f;
        if (bc==-11) s=-1.f;

        //Transform incident field trace to BB modal space and add into positive trace
        for (iint n=0; n<mesh->Nfp[N]; n++){
          dfloat sourceu = 0.0;
          dfloat sourcev = 0.0;
          dfloat sourcew = 0.0;
          dfloat sourcep = 0.0;
          for (iint m=0; m<mesh->Nfp[N]; m++){
            sourceu += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+0];
            sourcev += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+1];
            sourcew += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+2];
            sourcep += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+3];
          }
          //adjust positive trace values with the incident field
          iint id  = e*mesh->Nfaces*mesh->NfpMax + f*mesh->NfpMax + n;
          iint qidM = mesh->Nfields*mesh->vmapM[id];

          iint qid = mesh->Nfields*id;
          un[n] += s*sourceu;
          vn[n] += s*sourcev;
          wn[n] += s*sourcew;
          pn[n] += s*sourcep;
        }
      }

      //load local traces
      for (iint n=0;n<mesh->Nfp[N];n++) {
        iint id = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qidM = mesh->Nfields*mesh->vmapM[id];

        un[n] = mesh->q[qidM+0];
        vn[n] = mesh->q[qidM+1];
        wn[n] = mesh->q[qidM+2];
        pn[n] = mesh->q[qidM+3];
      }

      // load element neighbour
      iint eP = mesh->EToE[e*mesh->Nfaces+f];
      if (eP<0) eP = e; //boundary
      iint NP = mesh->N[eP]; 

      if (NP > N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<3;m++){ //apply raise operator sparsly
            dfloat BBRaiseVal = mesh->BBRaiseVals[N][3*n+m];
            iint BBRaiseid = mesh->BBRaiseids[N][3*n+m];
            unp[n] += BBRaiseVal*un[BBRaiseid];
            vnp[n] += BBRaiseVal*vn[BBRaiseid];
            wnp[n] += BBRaiseVal*wn[BBRaiseid];
            pnp[n] += BBRaiseVal*pn[BBRaiseid];
          }
        }
      } else if (NP < N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<mesh->Nfp[N];m++){
            iint id = n*mesh->Nfp[N] + m;
            unp[n] += mesh->BBLower[N][id]*un[m];
            vnp[n] += mesh->BBLower[N][id]*vn[m];
            wnp[n] += mesh->BBLower[N][id]*wn[m];
            pnp[n] += mesh->BBLower[N][id]*pn[m];
          }
        }
      } else { //equal order neighbor
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = un[n];
          vnp[n] = vn[n];
          wnp[n] = wn[n];
          pnp[n] = pn[n];
        }
      }

      //write new traces to fQ
      for (iint n=0;n<mesh->Nfp[NP];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qid = mesh->Nfields*id;

        mesh->fQP[qid+0] = unp[n];
        mesh->fQP[qid+1] = vnp[n];
        mesh->fQP[qid+2] = wnp[n];
        mesh->fQP[qid+3] = pnp[n];
      }
    }
  }

  //rotate index
  mesh->MRABshiftIndex[lev] = (mesh->MRABshiftIndex[lev]+1)%3;

  free(un); free(vn); free(wn); free(pn);
  free(unp); free(vnp); free(wnp); free(pnp);
  free(p);
  free(sourceq);
}

void acousticsMRABUpdateTrace3D_wadg(mesh3D *mesh,  
                           dfloat a1,
                           dfloat a2,
                           dfloat a3, iint lev, dfloat t, dfloat dt){
  
  dfloat *un = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pn = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *unp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *vnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *wnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));
  dfloat *pnp = (dfloat*) calloc(mesh->NfpMax,sizeof(dfloat));

  dfloat *sourceq = (dfloat *) calloc(mesh->Nfields*mesh->NfpMax,sizeof(dfloat));
  dfloat *s_q = (dfloat*) calloc(mesh->NpMax*mesh->Nfields,sizeof(dfloat));
  dfloat *p   = (dfloat*) calloc(mesh->cubNpMax,sizeof(dfloat));

  for(iint et=0;et<mesh->MRABNelements[lev];et++){
    iint e = mesh->MRABelementIds[lev][et];
    iint N = mesh->N[e];

    dfloat rhsqn[mesh->Nfields];

    // Interpolate rhs to cubature nodes
    for(iint n=0;n<mesh->cubNp[N];++n){
      p[n] = 0.f;
      iint id = mesh->Nfields*(e*mesh->NpMax);
      iint rhsId = 3*id + mesh->MRABshiftIndex[lev]*mesh->Nfields;

      for (iint i=0;i<mesh->Np[N];++i){
        p[n] += mesh->cubInterp[N][n*mesh->Np[N] + i] * mesh->rhsq[rhsId + 3*mesh->Nfields*i + 3];
      }

      // Multiply result by wavespeed c2 at cubature node
      p[n] *= mesh->c2[n + e*mesh->cubNpMax];
    }

    // Increment solution, project result back down
    for(iint n=0;n<mesh->Np[N];++n){
      // Extract velocity rhs
      iint id = mesh->Nfields*(e*mesh->NpMax + n);
      iint rhsId = 3*id + mesh->MRABshiftIndex[lev]*mesh->Nfields;
      
      // Project scaled rhs down
      dfloat rhsp = 0.f;
      for (iint i=0;i<mesh->cubNp[N];++i){
        rhsp += mesh->cubProject[N][n*mesh->cubNp[N] + i] * p[i];
      }
      rhsqn[0] = mesh->rhsq[rhsId + 0];
      rhsqn[1] = mesh->rhsq[rhsId + 1];
      rhsqn[2] = mesh->rhsq[rhsId + 2];  
      rhsqn[3] = rhsp;

      iint rhsId1 = 3*id + ((mesh->MRABshiftIndex[lev]+0)%3)*mesh->Nfields;
      iint rhsId2 = 3*id + ((mesh->MRABshiftIndex[lev]+1)%3)*mesh->Nfields;
      iint rhsId3 = 3*id + ((mesh->MRABshiftIndex[lev]+2)%3)*mesh->Nfields;
      for (iint fld=0;fld<mesh->Nfields;fld++)
        s_q[n*mesh->Nfields+fld] = mesh->q[id+fld] + dt*(a1*rhsqn[fld] + a2*mesh->rhsq[rhsId2+fld] + a3*mesh->rhsq[rhsId3+fld]);
    }


    //project traces to proper order for neighbour
    for (iint f =0;f<mesh->Nfaces;f++) {
      //load local traces
      for (iint n=0;n<mesh->Nfp[N];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qidM = mesh->Nfields*(mesh->vmapM[id]-e*mesh->NpMax);
        iint qid = mesh->Nfields*id;

        un[n] = s_q[qidM+0];
        vn[n] = s_q[qidM+1];
        wn[n] = s_q[qidM+2];
        pn[n] = s_q[qidM+3];
      
        mesh->fQM[qid+0] = un[n];
        mesh->fQM[qid+1] = vn[n];
        mesh->fQM[qid+2] = wn[n];
        mesh->fQM[qid+3] = pn[n];
      }

      //check if this face is an interface of the source injection region
      iint bc = mesh->EToB[e*mesh->Nfaces+f];
      if ((bc==-10)||(bc==-11)) {
        for (iint n=0;n<mesh->Nfp[N];n++) {
          iint id = n + f*mesh->NfpMax + e*mesh->Nfaces*mesh->NfpMax;
          iint idM = mesh->vmapM[id];

          //get the nodal values of the incident field along the trace
          dfloat x = mesh->x[idM];
          dfloat y = mesh->y[idM];
          dfloat z = mesh->z[idM];

          dfloat x0 = mesh->sourceX0;
          dfloat y0 = mesh->sourceY0;
          dfloat z0 = mesh->sourceZ0;
          dfloat t0 = mesh->sourceT0;
          dfloat freq = mesh->sourceFreq;

          dfloat c = sqrt(mesh->sourceC2);

          int qid = mesh->Nfields*n;
          acousticsRickerPulse3D(x-x0, y-y0, z-z0, t+t0, freq,c, sourceq+qid+0, sourceq+qid+1, sourceq+qid+2, sourceq+qid+3);
        }

        dfloat s = 0.f;
        if (bc==-10) s= 1.f;
        if (bc==-11) s=-1.f;

        //Transform incident field trace to BB modal space and add into positive trace
        for (iint n=0; n<mesh->Nfp[N]; n++){
          dfloat sourceu = 0.0;
          dfloat sourcev = 0.0;
          dfloat sourcew = 0.0;
          dfloat sourcep = 0.0;
          for (iint m=0; m<mesh->Nfp[N]; m++){
            sourceu += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+0];
            sourcev += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+1];
            sourcew += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+2];
            sourcep += mesh->invVB2D[N][n*mesh->Nfp[N]+m]*sourceq[m*mesh->Nfields+3];
          }
          //adjust positive trace values with the incident field
          iint id  = e*mesh->Nfaces*mesh->NfpMax + f*mesh->NfpMax + n;
          iint qidM = mesh->Nfields*mesh->vmapM[id];

          iint qid = mesh->Nfields*id;
          un[n] += s*sourceu;
          vn[n] += s*sourcev;
          wn[n] += s*sourcew;
          pn[n] += s*sourcep;
        }
      }

      // load element neighbour
      iint eP = mesh->EToE[e*mesh->Nfaces+f];
      if (eP<0) eP = e; //boundary
      iint NP = mesh->N[eP]; 

      if (NP > N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<3;m++){ //apply raise operator sparsly
            dfloat BBRaiseVal = mesh->BBRaiseVals[N][3*n+m];
            iint BBRaiseid = mesh->BBRaiseids[N][3*n+m];
            unp[n] += BBRaiseVal*un[BBRaiseid];
            vnp[n] += BBRaiseVal*vn[BBRaiseid];
            wnp[n] += BBRaiseVal*wn[BBRaiseid];
            pnp[n] += BBRaiseVal*pn[BBRaiseid];
          }
        }
      } else if (NP < N) { 
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = 0.0;
          vnp[n] = 0.0;
          wnp[n] = 0.0;
          pnp[n] = 0.0;
          for (iint m=0;m<mesh->Nfp[N];m++){
            iint id = n*mesh->Nfp[N] + m;
            unp[n] += mesh->BBLower[N][id]*un[m];
            vnp[n] += mesh->BBLower[N][id]*vn[m];
            wnp[n] += mesh->BBLower[N][id]*wn[m];
            pnp[n] += mesh->BBLower[N][id]*pn[m];
          }
        }
      } else { //equal order neighbor
        for (iint n=0;n<mesh->Nfp[NP];n++){
          unp[n] = un[n];
          vnp[n] = vn[n];
          wnp[n] = wn[n];
          pnp[n] = pn[n];
        }
      }

      //write new traces to fQ
      for (iint n=0;n<mesh->Nfp[NP];n++) {
        iint id  = e*mesh->NfpMax*mesh->Nfaces + f*mesh->NfpMax + n;
        iint qid = mesh->Nfields*id;

        mesh->fQP[qid+0] = unp[n];
        mesh->fQP[qid+1] = vnp[n];
        mesh->fQP[qid+2] = wnp[n];
        mesh->fQP[qid+3] = pnp[n];
      }
    }
  }

  free(un); free(vn); free(wn); free(pn);
  free(unp); free(vnp); free(wnp); free(pnp);
  free(s_q); free(p);
  free(sourceq);
}

//Ricker pulse
dfloat ricker(dfloat t, dfloat f) {
  return (1-2*M_PI*M_PI*f*f*t*t)*exp(-M_PI*M_PI*f*f*t*t);
}

//integrated Ricker pulse
dfloat intRicker(dfloat t, dfloat f) {
  return t*exp(-M_PI*M_PI*f*f*t*t);
}

void acousticsRickerPulse3D(dfloat x, dfloat y, dfloat z, dfloat t, dfloat f, dfloat c,
                           dfloat *u, dfloat *v, dfloat *w, dfloat *p) {

  //radial distance
  dfloat r = mymax(sqrt(x*x+y*y+z*z),1e-9);

  *p = ricker(t - r/c,f)/(4*M_PI*r);
  *u = x*(intRicker(t-r/c,f)/r + ricker(t-r/c,f)/c)/(4*M_PI*r*r);
  *v = y*(intRicker(t-r/c,f)/r + ricker(t-r/c,f)/c)/(4*M_PI*r*r);
  *w = z*(intRicker(t-r/c,f)/r + ricker(t-r/c,f)/c)/(4*M_PI*r*r);
}