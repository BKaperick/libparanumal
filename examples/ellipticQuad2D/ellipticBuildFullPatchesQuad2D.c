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

#include "ellipticQuad2D.h"

typedef struct {

  int signature[4*4];
  dlong id;

} refPatch_t;

void matrixInverse(int N, dfloat *A);

dfloat matrixConditionNumber(int N, dfloat *A);


void BuildFullPatchAx(mesh2D *mesh, dfloat tau, dfloat lambda, int* BCType,
                        dfloat *B, dfloat *Br, dfloat *Bs, dlong eM, dfloat *A);

void BuildReferenceFullPatch(mesh2D *mesh, dfloat tau, dfloat lambda, int* BCType,
                        dfloat *B, dfloat *Br, dfloat *Bs, int *signature, dfloat *A);

dlong getFullPatchIndex(refPatch_t *referencePatchList, dlong numRefPatches, int *signature);



void ellipticBuildFullPatchesQuad2D(solver_t *solver, dfloat tau, dfloat lambda, int *BCType, dfloat rateTolerance,
                                   dlong *Npatches, dlong **patchesIndex, dfloat **patchesInvA,
                                   const char *options){

  mesh2D *mesh = solver->mesh;

  // build some monolithic basis arrays
  dfloat *B  = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  dfloat *Br = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  dfloat *Bs = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));

  int mode = 0;
  for(int nj=0;nj<mesh->N+1;++nj){
    for(int ni=0;ni<mesh->N+1;++ni){

      int node = 0;

      for(int j=0;j<mesh->N+1;++j){
        for(int i=0;i<mesh->N+1;++i){

          if(nj==j && ni==i)
            B[mode*mesh->Np+node] = 1;
          if(nj==j)
            Br[mode*mesh->Np+node] = mesh->D[ni+mesh->Nq*i]; 
          if(ni==i)
            Bs[mode*mesh->Np+node] = mesh->D[nj+mesh->Nq*j]; 
          
          ++node;
        }
      }
      ++mode;
    }
  }

  //We need the halo element's EToB flags to make the patch matrices
  if (mesh->totalHaloPairs) {
    mesh->EToB = (int *) realloc(mesh->EToB,(mesh->Nelements+mesh->totalHaloPairs)*sizeof(int));
    int *idSendBuffer = (int *) calloc(mesh->totalHaloPairs,sizeof(int));
    meshHaloExchange(mesh, sizeof(int), mesh->EToB, idSendBuffer, mesh->EToB + mesh->Nelements);
    free(idSendBuffer);
  }

  int NpatchElements = mesh->Nfaces+1;
  int patchNp = mesh->Np*NpatchElements;

  (*Npatches) = 0;
  dlong numRefPatches=0;
  dlong refPatches = 0;

  //patch inverse storage
  *patchesInvA = (dfloat*) calloc((*Npatches)*patchNp*patchNp, sizeof(dfloat));
  *patchesIndex = (dlong*) calloc(mesh->Nelements, sizeof(dlong));

  refPatch_t *referencePatchList = (refPatch_t *) calloc(numRefPatches,sizeof(refPatch_t));

  //temp patch storage
  dfloat *patchA = (dfloat*) calloc(patchNp*patchNp, sizeof(dfloat));
  dfloat *invRefAA = (dfloat*) calloc(patchNp*patchNp, sizeof(dfloat));

  dfloat *refPatchInvA;
  // loop over all elements
  for(dlong eM=0;eM<mesh->Nelements;++eM){

    //build the patch A matrix for this element
    BuildFullPatchAx(mesh, tau, lambda, BCType, B, Br, Bs, eM, patchA);

    dlong eP0 = mesh->EToE[eM*mesh->Nfaces+0];
    dlong eP1 = mesh->EToE[eM*mesh->Nfaces+1];
    dlong eP2 = mesh->EToE[eM*mesh->Nfaces+2];
    dlong eP3 = mesh->EToE[eM*mesh->Nfaces+3];

    if(eP0>=0 && eP1>=0 && eP2>=0 && eP3>=0){ //check if this is an interiour patch

      //get the vertices
      hlong *vM = mesh->EToV + eM*mesh->Nverts;
      hlong *vP0 = mesh->EToV + eP0*mesh->Nverts;
      hlong *vP1 = mesh->EToV + eP1*mesh->Nverts;
      hlong *vP2 = mesh->EToV + eP2*mesh->Nverts;
      hlong *vP3 = mesh->EToV + eP3*mesh->Nverts;

      hlong *vP[4] = {vP0,vP1,vP2,vP3};

      // intialize signature to -1
      int signature[4*4];
      for (int n=0;n<mesh->Nfaces*mesh->Nverts;n++) signature[n] = -1;

      for (int f=0;f<mesh->Nfaces;f++) {
        for (int n=0;n<mesh->Nverts;n++) {
          for (int m=0;m<mesh->Nverts;m++) {
            if (vP[f][m] == vM[n]) signature[f*mesh->Nverts + m] = n; 
          }
        }
      }

      dlong index = getFullPatchIndex(referencePatchList,numRefPatches,signature);
      if (index<0) {      
        //build the reference patch for this signature
        ++(*Npatches);
        numRefPatches++;
        *patchesInvA = (dfloat*) realloc(*patchesInvA, (*Npatches)*patchNp*patchNp*sizeof(dfloat));
        referencePatchList = (refPatch_t *) realloc(referencePatchList,numRefPatches*sizeof(refPatch_t)); 
        referencePatchList[numRefPatches-1].id = (*Npatches)-1;
        for (int n=0;n<mesh->Nverts*mesh->Nfaces;n++) 
          referencePatchList[numRefPatches-1].signature[n] = signature[n];

        refPatchInvA = *patchesInvA + ((*Npatches)-1)*patchNp*patchNp;

        // printf("Building reference patch with signature [%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d] \n", 
                  // signature[0], signature[1], signature[2],signature[3],
                  // signature[4], signature[5], signature[6],signature[7],
                  // signature[8], signature[9], signature[10],signature[11],
                  // signature[12], signature[13], signature[14],signature[15]);

        BuildReferenceFullPatch(mesh, tau, lambda, BCType, B, Br, Bs, signature, refPatchInvA); 
        matrixInverse(patchNp, refPatchInvA);        
        index = (*Npatches)-1;
      }

      refPatchInvA = *patchesInvA + index*patchNp*patchNp;

      //hit the patch with the reference inverse
      for(int n=0;n<patchNp;++n){
        for(int m=0;m<patchNp;++m){
          invRefAA[n*patchNp+m] = 0.;
          for (int k=0;k<patchNp;k++) {
            invRefAA[n*patchNp+m] += refPatchInvA[n*patchNp+k]*patchA[k*patchNp+m];
          }
        }
      }

      dfloat cond = matrixConditionNumber(patchNp,invRefAA);
      dfloat rate = (sqrt(cond)-1.)/(sqrt(cond)+1.);

      // printf("Element "dlongFormat"'s conditioned patch reports cond = %g and rate = %g \n", eM, cond, rate);
      // printf("Element "dlongFormat" has signature [%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d] \n", eM,
      //             signature[0], signature[1], signature[2],signature[3],
      //             signature[4], signature[5], signature[6],signature[7],
      //             signature[8], signature[9], signature[10],signature[11],
      //             signature[12], signature[13], signature[14],signature[15]);


      if (rate < rateTolerance) {
        (*patchesIndex)[eM] = index;
        refPatches++;
        continue;
      }
    }
        
    //add this patch to the patch list
    ++(*Npatches);
    *patchesInvA = (dfloat*) realloc(*patchesInvA, (*Npatches)*patchNp*patchNp*sizeof(dfloat));

    matrixInverse(patchNp, patchA);

    //copy inverse into patchesInvA
    for(int n=0;n<patchNp;++n){
      for(int m=0;m<patchNp;++m){
        dlong id = ((*Npatches)-1)*patchNp*patchNp + n*patchNp + m;
        (*patchesInvA)[id] = patchA[n*patchNp+m];
      }
    }

    (*patchesIndex)[eM] = (*Npatches)-1;
  }

  printf("saving "dlongFormat" full patches\n",*Npatches);
  printf("using "dlongFormat" reference patches\n", refPatches);

  free(patchA); free(invRefAA);
  free(B); free(Br); free(Bs);
}

void BuildFullPatchAx(mesh2D *mesh, dfloat tau, dfloat lambda, int* BCType,
                        dfloat *B, dfloat *Br, dfloat *Bs, dlong eM, dfloat *A) {;

  int NpatchElements = mesh->Nfaces+1;
  int patchNp = NpatchElements*mesh->Np;

  // Extract patches
    // *  a b c d
    // a' * 0 0 0
    // b' 0 * 0 0
    // c' 0 0 * 0
    // d' 0 0 0 *

  //zero out the matrix
  for (int n=0;n<patchNp*patchNp;n++) A[n] = 0.;

  //make sure the diagonal is at least identity
  for (int n=0;n<patchNp;n++) A[n*patchNp+n] = 1.;

  //start with diagonals
  for(int N=0;N<NpatchElements;++N){
    dlong e = (N==0) ? eM : mesh->EToE[mesh->Nfaces*eM+N-1];

    if (e<0) continue; //skip this block if this is a boundary face

    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        int id = N*mesh->Np*patchNp + n*patchNp + N*mesh->Np + m;
        A[id] = 0;

        // (grad phi_n, grad phi_m)_{D^e}
        for(int i=0;i<mesh->Np;++i){
          dlong base = e*mesh->Np*mesh->Nvgeo + i;
          dfloat drdx = mesh->vgeo[base+mesh->Np*RXID];
          dfloat drdy = mesh->vgeo[base+mesh->Np*RYID];
          dfloat dsdx = mesh->vgeo[base+mesh->Np*SXID];
          dfloat dsdy = mesh->vgeo[base+mesh->Np*SYID];
          dfloat JW   = mesh->vgeo[base+mesh->Np*JWID];
          
          int idn = n*mesh->Np+i;
          int idm = m*mesh->Np+i;
          dfloat dlndx = drdx*Br[idn] + dsdx*Bs[idn];
          dfloat dlndy = drdy*Br[idn] + dsdy*Bs[idn];
          dfloat dlmdx = drdx*Br[idm] + dsdx*Bs[idm];
          dfloat dlmdy = drdy*Br[idm] + dsdy*Bs[idm];
          A[id] += JW*(dlndx*dlmdx+dlndy*dlmdy);
          A[id] += lambda*JW*B[idn]*B[idm];
        }
    

        for (int fM=0;fM<mesh->Nfaces;fM++) {
          // accumulate flux terms for negative and positive traces
          for(int i=0;i<mesh->Nfp;++i){
            int vidM = mesh->faceNodes[i+fM*mesh->Nfp];

            // grab vol geofacs at surface nodes
            dlong baseM = eM*mesh->Np*mesh->Nvgeo + vidM;
            dfloat drdxM = mesh->vgeo[baseM+mesh->Np*RXID];
            dfloat drdyM = mesh->vgeo[baseM+mesh->Np*RYID];
            dfloat dsdxM = mesh->vgeo[baseM+mesh->Np*SXID];
            dfloat dsdyM = mesh->vgeo[baseM+mesh->Np*SYID];

            // grab surface geometric factors
            dlong base = mesh->Nsgeo*(eM*mesh->Nfp*mesh->Nfaces + fM*mesh->Nfp + i);
            dfloat nx = mesh->sgeo[base+NXID];
            dfloat ny = mesh->sgeo[base+NYID];
            dfloat wsJ = mesh->sgeo[base+WSJID];
            dfloat hinv = mesh->sgeo[base+IHID];
            
            // form negative trace terms in IPDG
            int idnM = n*mesh->Np+vidM; 
            int idmM = m*mesh->Np+vidM;

            dfloat dlndxM = drdxM*Br[idnM] + dsdxM*Bs[idnM];
            dfloat dlndyM = drdyM*Br[idnM] + dsdyM*Bs[idnM];
            dfloat ndotgradlnM = nx*dlndxM+ny*dlndyM;
            dfloat lnM = B[idnM];

            dfloat dlmdxM = drdxM*Br[idmM] + dsdxM*Bs[idmM];
            dfloat dlmdyM = drdyM*Br[idmM] + dsdyM*Bs[idmM];
            dfloat ndotgradlmM = nx*dlmdxM+ny*dlmdyM;
            dfloat lmM = B[idmM];
            
            dfloat penalty = tau*hinv;     
            int bc = mesh->EToB[fM+mesh->Nfaces*e]; //raw boundary flag

            int bcD = 0, bcN =0;
            int bcType = 0;

            if(bc>0) bcType = BCType[bc];          //find its type (Dirichlet/Neumann)

            // this needs to be double checked (and the code where these are used)
            if(bcType==1){ // Dirichlet
              bcD = 1;
              bcN = 0;
            } else if(bcType==2){ // Neumann
              bcD = 0;
              bcN = 1;
            }   

            A[id] += -0.5*(1+bcD)*(1-bcN)*wsJ*lnM*ndotgradlmM;  // -(ln^-, N.grad lm^-)
            A[id] += -0.5*(1+bcD)*(1-bcN)*wsJ*ndotgradlnM*lmM;  // -(N.grad ln^-, lm^-)
            A[id] += +0.5*(1+bcD)*(1-bcN)*wsJ*penalty*lnM*lmM; // +((tau/h)*ln^-,lm^-)
          }
        }
      }
    }
  }

  //now the off-diagonals
  for (int fM=0;fM<mesh->Nfaces;fM++) {

    dlong eP = mesh->EToE[eM*mesh->Nfaces+fM];
    if (eP < 0) continue; //skip this block if this is a boundary face
  
    // accumulate flux terms for positive traces
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        int id = n*patchNp + (fM+1)*mesh->Np + m;
        A[id] = 0;
        
        for(int i=0;i<mesh->Nfp;++i){
          int vidM = mesh->faceNodes[i+fM*mesh->Nfp];

          // grab vol geofacs at surface nodes
          dlong baseM = eM*mesh->Np*mesh->Nvgeo + vidM;
          dfloat drdxM = mesh->vgeo[baseM+mesh->Np*RXID];
          dfloat drdyM = mesh->vgeo[baseM+mesh->Np*RYID];
          dfloat dsdxM = mesh->vgeo[baseM+mesh->Np*SXID];
          dfloat dsdyM = mesh->vgeo[baseM+mesh->Np*SYID];

          // double check vol geometric factors are in halo storage of vgeo
          dlong idM     = eM*mesh->Nfp*mesh->Nfaces+fM*mesh->Nfp+i;
          int vidP    = (int) (mesh->vmapP[idM]%mesh->Np); // only use this to identify location of positive trace vgeo
          dlong baseP   = eP*mesh->Np*mesh->Nvgeo + vidP; // use local offset for vgeo in halo
          dfloat drdxP = mesh->vgeo[baseP+mesh->Np*RXID];
          dfloat drdyP = mesh->vgeo[baseP+mesh->Np*RYID];
          dfloat dsdxP = mesh->vgeo[baseP+mesh->Np*SXID];
          dfloat dsdyP = mesh->vgeo[baseP+mesh->Np*SYID];
          
          // grab surface geometric factors
          dlong base = mesh->Nsgeo*(eM*mesh->Nfp*mesh->Nfaces + fM*mesh->Nfp + i);
          dfloat nx = mesh->sgeo[base+NXID];
          dfloat ny = mesh->sgeo[base+NYID];
          dfloat wsJ = mesh->sgeo[base+WSJID];
          dfloat hinv = mesh->sgeo[base+IHID];
          
          // form trace terms in IPDG
          int idnM = n*mesh->Np+vidM; 
          int idmP = m*mesh->Np+vidP;

          dfloat dlndxM = drdxM*Br[idnM] + dsdxM*Bs[idnM];
          dfloat dlndyM = drdyM*Br[idnM] + dsdyM*Bs[idnM];
          dfloat ndotgradlnM = nx*dlndxM+ny*dlndyM;
          dfloat lnM = B[idnM];
          
          dfloat dlmdxP = drdxP*Br[idmP] + dsdxP*Bs[idmP];
          dfloat dlmdyP = drdyP*Br[idmP] + dsdyP*Bs[idmP];
          dfloat ndotgradlmP = nx*dlmdxP+ny*dlmdyP;
          dfloat lmP = B[idmP];
          
          dfloat penalty = tau*hinv;     
          
          A[id] += -0.5*wsJ*lnM*ndotgradlmP;  // -(ln^-, N.grad lm^+)
          A[id] += +0.5*wsJ*ndotgradlnM*lmP;  // +(N.grad ln^-, lm^+)
          A[id] += -0.5*wsJ*penalty*lnM*lmP; // -((tau/h)*ln^-,lm^+)
        }
      }
    }

    //write the transpose of the off-diagonal block
    for(int n=0;n<mesh->Np;++n){
      for(int m=0;m<mesh->Np;++m){
        int id  = n*patchNp + (fM+1)*mesh->Np + m;
        int idT = (fM+1)*mesh->Np*patchNp + m*patchNp + n;

        A[idT] = A[id];
      }
    }
  }
}

void BuildReferenceFullPatch(mesh2D *mesh, dfloat tau, dfloat lambda, int* BCType,
                        dfloat *B, dfloat *Br, dfloat *Bs, int *signature, dfloat *A) {
  //build a mini mesh struct for the reference patch
  mesh2D *refMesh = (mesh2D*) calloc(1,sizeof(mesh2D));
  memcpy(refMesh,mesh,sizeof(mesh2D));

   //vertices of reference patch
  int Nv = 12;
  dfloat VX[12] = {-1, 1, 1,-1,-1, 1, 3, 3, 1,-1,-3,-3};
  dfloat VY[12] = {-1,-1, 1, 1,-3,-3,-1, 1, 3, 3, 1,-1};

  hlong EToV[5*4] = {0,1,2,3,
                    1,0,4,5,
                    2,1,6,7,
                    3,2,8,9,
                    0,3,10,11};

  int NpatchElements = 5;                    
  refMesh->Nelements = NpatchElements;

  refMesh->EX = (dfloat *) calloc(mesh->Nverts*NpatchElements,sizeof(dfloat));
  refMesh->EY = (dfloat *) calloc(mesh->Nverts*NpatchElements,sizeof(dfloat));

  refMesh->EToV = (hlong*) calloc(NpatchElements*mesh->Nverts, sizeof(hlong));

  for(int n=0;n<mesh->Nverts;++n){
    hlong v = EToV[n];
    refMesh->EX[n] = VX[v];
    refMesh->EY[n] = VY[v];
    refMesh->EToV[n] = v;
  } 

  int cnt[4] = {0,0,0,0};
  for (int f=0;f<mesh->Nfaces;f++) {
    for (int n=0;n<mesh->Nverts;n++) {
      if (signature[f*mesh->Nverts + n]==-1) {
        //fill the missing vertex based ont he face number
        hlong v = EToV[(f+1)*mesh->Nverts+mesh->Nverts-2 + cnt[f]];  
        refMesh->EX[(f+1)*mesh->Nverts+n] = VX[v];
        refMesh->EY[(f+1)*mesh->Nverts+n] = VY[v];
        refMesh->EToV[(f+1)*mesh->Nverts+n] = v; //extra vert      
        cnt[f]++;
      } else {
        int v = signature[f*mesh->Nverts + n];  
        refMesh->EX[(f+1)*mesh->Nverts+n] = VX[v];
        refMesh->EY[(f+1)*mesh->Nverts+n] = VY[v];
        refMesh->EToV[(f+1)*mesh->Nverts+n] = v;      
      }
    }  
  }

  refMesh->EToB = (int*) calloc(NpatchElements*mesh->Nfaces,sizeof(int));
  for (int n=0;n<NpatchElements*mesh->Nfaces;n++) refMesh->EToB[n] = 0;

  meshConnect(refMesh);
  meshLoadReferenceNodesQuad2D(refMesh, mesh->N);
  meshPhysicalNodesQuad2D(refMesh);
  meshGeometricFactorsQuad2D(refMesh);
  meshConnectFaceNodes2D(refMesh);
  meshSurfaceGeometricFactorsQuad2D(refMesh);

  //build this reference patch
  BuildFullPatchAx(refMesh, tau, lambda, BCType, B, Br, Bs, 0, A);

  free(refMesh->EX);
  free(refMesh->EY);
  free(refMesh->EToV);
  free(refMesh->EToB);

  free(refMesh);
}

dlong getFullPatchIndex(refPatch_t *referencePatchList, dlong numRefPatches, int *signature) {

  dlong index = -1;
  for (int n=0;n<numRefPatches;n++) {
    bool match = true;
    for (int m=0;m<4*4;m++) {
      match = match && (referencePatchList[n].signature[n] == signature[n]);
    }
    if (match) {
      index = referencePatchList[n].id;
      break;
    }
  }
  return index;
}

void matrixInverse(int N, dfloat *A){
  int lwork = N*N;
  int info;

  // compute inverse mass matrix
  double *tmpInvA = (double*) calloc(N*N, sizeof(double));

  int *ipiv = (int*) calloc(N, sizeof(int));
  double *work = (double*) calloc(lwork, sizeof(double));

  for(int n=0;n<N*N;++n){
    tmpInvA[n] = A[n];
  }

  dgetrf_ (&N, &N, tmpInvA, &N, ipiv, &info);
  dgetri_ (&N, tmpInvA, &N, ipiv, work, &lwork, &info);

  if(info)
    printf("inv: dgetrf/dgetri reports info = %d when inverting matrix\n", info);

  for(int n=0;n<N*N;++n)
    A[n] = tmpInvA[n];

  free(work);
  free(ipiv);
  free(tmpInvA);
}

dfloat matrixConditionNumber(int N, dfloat *A) {

  int lwork = 4*N;
  int info;

  char norm = '1';

  double Acond;
  double Anorm;

  double *tmpLU = (double*) calloc(N*N, sizeof(double));

  int *ipiv = (int*) calloc(N, sizeof(int));
  double *work = (double*) calloc(lwork, sizeof(double));
  int  *iwork = (int*) calloc(N, sizeof(int));

  for(int n=0;n<N*N;++n){
    tmpLU[n] = (double) A[n];
  }

  //get the matrix norm of A
  Anorm = dlange_(&norm, &N, &N, tmpLU, &N, work);

  //compute LU factorization
  dgetrf_ (&N, &N, tmpLU, &N, ipiv, &info);

  //compute inverse condition number
  dgecon_(&norm, &N, tmpLU, &N, &Anorm, &Acond, work, iwork, &info);

  if(info)
    printf("inv: dgetrf/dgecon reports info = %d when computing condition number\n", info);

  free(work);
  free(iwork);
  free(ipiv);
  free(tmpLU);

  return (dfloat) 1.0/Acond;
}
