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

#include "ellipticTri2D.h"

typedef struct{

  dfloat VX;
  dfloat VY;

  dlong localId;
  hlong globalId;

}FEMverts_t;

typedef struct {

  dlong localId;
  hlong globalId;
  int ownerRank;

}parallelNode_t;

// compare on global owners
int parallelCompareOwnersAndGlobalId(const void *a, const void *b);

// compare on global indices
int parallelCompareGlobalId(const void *a, const void *b);

// compare xy coordinates
int parallelCompareFEMvertsLocation(const void *a, const void *b){
  dfloat NODETOL = 1e-6;

  FEMverts_t *fa = (FEMverts_t*) a;
  FEMverts_t *fb = (FEMverts_t*) b;

  if(fa->VX < fb->VX - NODETOL) return -1;
  if(fa->VX > fb->VX + NODETOL) return +1;

  if(fa->VY < fb->VY - NODETOL) return -1;
  if(fa->VY > fb->VY + NODETOL) return +1;

  return 0;
}

// compare local id
int parallelCompareFEMvertsLocalId(const void *a, const void *b){

  FEMverts_t *fa = (FEMverts_t*) a;
  FEMverts_t *fb = (FEMverts_t*) b;

  if(fa->localId < fb->localId) return -1;
  if(fa->localId > fb->localId) return +1;

  return 0;
}

int parallelCompareRowColumn(const void *a, const void *b);

void ellipticSEMFEMSetupTri2D(solver_t *solver, precon_t* precon,
                              dfloat tau, dfloat lambda, int *BCType,
                              const char *options, const char *parAlmondOptions) {

  if (!strstr(options, "CONTINUOUS")) {
    printf("SEMFEM is supported for CONTINUOUS only\n");
    exit(-1);
  }

  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  mesh2D* mesh = solver->mesh; //original mesh

  mesh2D* pmesh = (mesh2D*) calloc (1,sizeof(mesh2D)); //partially assembled fem mesh (result of projecting sem element to larger space)

  precon->femMesh = (mesh2D*) calloc (1,sizeof(mesh2D)); //full fem mesh
  mesh2D *femMesh = precon->femMesh;

  memcpy(pmesh  ,mesh,sizeof(mesh2D));
  memcpy(femMesh,mesh,sizeof(mesh2D));

  //set semfem nodes as the grid points
  pmesh->Np = mesh->NpFEM;
  pmesh->r  = mesh->rFEM;
  pmesh->s  = mesh->sFEM;

  //count number of face nodes in the semfem element
  dfloat NODETOL = 1e-6;
  pmesh->Nfp=0;
  for (int n=0;n<pmesh->Np;n++)
    if (fabs(pmesh->s[n]+1)<NODETOL) pmesh->Nfp++;

  //remake the faceNodes array
  pmesh->faceNodes = (int *) calloc(pmesh->Nfaces*pmesh->Nfp,sizeof(int));
  int f0=0, f1=0, f2=0;
  for (int n=0;n<pmesh->Np;n++) {
    if (fabs(pmesh->s[n]+1)<NODETOL)           pmesh->faceNodes[0*pmesh->Nfp+f0++] = n;
    if (fabs(pmesh->r[n]+pmesh->s[n])<NODETOL) pmesh->faceNodes[1*pmesh->Nfp+f1++] = n;
    if (fabs(pmesh->r[n]+1)<NODETOL)           pmesh->faceNodes[2*pmesh->Nfp+f2++] = n;
  }

  //remake vertexNodes array
  pmesh->vertexNodes = (int*) calloc(pmesh->Nverts, sizeof(int));
  for(int n=0;n<pmesh->Np;++n){
    if( (pmesh->r[n]+1)*(pmesh->r[n]+1)+(pmesh->s[n]+1)*(pmesh->s[n]+1)<NODETOL)
      pmesh->vertexNodes[0] = n;
    if( (pmesh->r[n]-1)*(pmesh->r[n]-1)+(pmesh->s[n]+1)*(pmesh->s[n]+1)<NODETOL)
      pmesh->vertexNodes[1] = n;
    if( (pmesh->r[n]+1)*(pmesh->r[n]+1)+(pmesh->s[n]-1)*(pmesh->s[n]-1)<NODETOL)
      pmesh->vertexNodes[2] = n;
  }

  // connect elements using parallel sort
  meshParallelConnect(pmesh);

  // compute physical (x,y) locations of the element nodes
  meshPhysicalNodesTri2D(pmesh);

  // free(sendBuffer);
  meshHaloSetup(pmesh);

  // connect face nodes (find trace indices)
  meshConnectFaceNodes2D(pmesh);

  // global nodes
  meshParallelConnectNodes(pmesh);
  //pmesh->globalIds and pmesh->globalOwners are now populated

  //now build the full degree 1 fem mesh
  int femN = 1; //degree of fem approximation

  /* allocate space for node coordinates */
  femMesh->Nelements = mesh->NelFEM*mesh->Nelements;
  femMesh->EToV = (hlong*) calloc(femMesh->Nelements*femMesh->Nverts, sizeof(hlong));
  femMesh->EX = (dfloat*) calloc(femMesh->Nverts*femMesh->Nelements, sizeof(dfloat));
  femMesh->EY = (dfloat*) calloc(femMesh->Nverts*femMesh->Nelements, sizeof(dfloat));

  dlong *localIds = (dlong *) calloc(femMesh->Nverts*femMesh->Nelements,sizeof(dlong));

  // dlong NFEMverts = mesh->Nelements*mesh->NpFEM;
  for(dlong e=0;e<mesh->Nelements;++e){
    for (int n=0;n<mesh->NelFEM;n++) {
      //local ids in the subelement fem grid
      dlong id1 = e*mesh->NpFEM + mesh->FEMEToV[n*mesh->Nverts+0];
      dlong id2 = e*mesh->NpFEM + mesh->FEMEToV[n*mesh->Nverts+1];
      dlong id3 = e*mesh->NpFEM + mesh->FEMEToV[n*mesh->Nverts+2];

      // check orientation
      dfloat xe1 = pmesh->x[id1], xe2 = pmesh->x[id2], xe3 = pmesh->x[id3];
      dfloat ye1 = pmesh->y[id1], ye2 = pmesh->y[id2], ye3 = pmesh->y[id3];
      dfloat J = 0.25*((xe2-xe1)*(ye3-ye1) - (xe3-xe1)*(ye2-ye1));
      if(J<0){
        dlong id3tmp = id3;
        id3 = id2;
        id2 = id3tmp;
      }

      /* read vertex triplet for triangle */
      dlong femId = e*mesh->NelFEM*mesh->Nverts+n*mesh->Nverts;
      femMesh->EToV[femId+0] = pmesh->globalIds[id1];
      femMesh->EToV[femId+1] = pmesh->globalIds[id2];
      femMesh->EToV[femId+2] = pmesh->globalIds[id3];

      femMesh->EX[femId+0] = pmesh->x[id1];
      femMesh->EX[femId+1] = pmesh->x[id2];
      femMesh->EX[femId+2] = pmesh->x[id3];

      femMesh->EY[femId+0] = pmesh->y[id1];
      femMesh->EY[femId+1] = pmesh->y[id2];
      femMesh->EY[femId+2] = pmesh->y[id3];

      localIds[femId+0] = id1;
      localIds[femId+1] = id2;
      localIds[femId+2] = id3;
    }
  }

  // connect elements using parallel sort
  meshParallelConnect(femMesh);

  // load reference (r,s) element nodes
  meshLoadReferenceNodesTri2D(femMesh, femN);

  // compute physical (x,y) locations of the element nodes
  meshPhysicalNodesTri2D(femMesh);

  // compute geometric factors
  meshGeometricFactorsTri2D(femMesh);

  // set up halo exchange info for MPI (do before connect face nodes)
  meshHaloSetup(femMesh);

  // connect face nodes (find trace indices)
  meshConnectFaceNodes2D(femMesh);

  // compute surface geofacs
  meshSurfaceGeometricFactorsTri2D(femMesh);

  // global nodes
  meshParallelConnectNodes(femMesh);


  //on-host version of gather-scatter
  int verbose = strstr(options,"VERBOSE") ? 1:0;
  pmesh->hostGsh = gsParallelGatherScatterSetup(pmesh->Nelements*pmesh->Np, pmesh->globalIds,verbose);

  //make a node-wise bc flag using the gsop (prioritize Dirichlet boundaries over Neumann)
  int *mapB = (int *) calloc(pmesh->Nelements*pmesh->Np,sizeof(int));
  for (dlong e=0;e<pmesh->Nelements;e++) {
    for (int n=0;n<pmesh->Np;n++) mapB[n+e*pmesh->Np] = 1E9;
    for (int f=0;f<pmesh->Nfaces;f++) {
      int bc = pmesh->EToB[f+e*pmesh->Nfaces];
      if (bc>0) {
        for (int n=0;n<pmesh->Nfp;n++) {
          int BCFlag = BCType[bc];
          int fid = pmesh->faceNodes[n+f*pmesh->Nfp];
          mapB[fid+e*pmesh->Np] = mymin(BCFlag,mapB[fid+e*pmesh->Np]);
        }
      }
    }
  }
  gsParallelGatherScatter(pmesh->hostGsh, mapB, "int", "min");

  //use the bc flags to find masked ids
  for (dlong n=0;n<pmesh->Nelements*pmesh->Np;n++) {
    if (mapB[n] == 1) { //Dirichlet boundary
      pmesh->globalIds[n] = -1;
    }
  }

  // squeeze node numbering
  hlong *globalStarts = (hlong*) calloc(size+1, sizeof(hlong));
  meshParallelConsecutiveGlobalNumbering(pmesh, pmesh->Np*pmesh->Nelements, pmesh->globalIds, pmesh->globalOwners, globalStarts);

  for (dlong n=0;n<pmesh->Np*pmesh->Nelements;n++) {
    dlong id = pmesh->gatherLocalIds[n];
    pmesh->gatherBaseIds[n] = pmesh->globalIds[id];
  }

  //build gather scatter with masked nodes
  precon->FEMogs = meshParallelGatherScatterSetup(pmesh, pmesh->Nelements*pmesh->Np, 
                                        pmesh->gatherLocalIds,  pmesh->gatherBaseIds, 
                                        pmesh->gatherBaseRanks, pmesh->gatherHaloFlags,verbose);



  //dont need these anymore
  free(pmesh->vmapM);
  free(pmesh->vmapP);
  free(pmesh->mapP);
  //maybe more cleanup can go here

  //build stiffness matrices
  femMesh->Srr = (dfloat *) calloc(femMesh->Np*femMesh->Np,sizeof(dfloat));
  femMesh->Srs = (dfloat *) calloc(femMesh->Np*femMesh->Np,sizeof(dfloat));
  femMesh->Ssr = (dfloat *) calloc(femMesh->Np*femMesh->Np,sizeof(dfloat));
  femMesh->Sss = (dfloat *) calloc(femMesh->Np*femMesh->Np,sizeof(dfloat));
  for (int n=0;n<femMesh->Np;n++) {
    for (int m=0;m<femMesh->Np;m++) {
      for (int k=0;k<femMesh->Np;k++) {
        for (int l=0;l<femMesh->Np;l++) {
          femMesh->Srr[m+n*femMesh->Np] += femMesh->Dr[n+l*femMesh->Np]*femMesh->MM[k+l*femMesh->Np]*femMesh->Dr[m+k*femMesh->Np];
          femMesh->Srs[m+n*femMesh->Np] += femMesh->Dr[n+l*femMesh->Np]*femMesh->MM[k+l*femMesh->Np]*femMesh->Ds[m+k*femMesh->Np];
          femMesh->Ssr[m+n*femMesh->Np] += femMesh->Ds[n+l*femMesh->Np]*femMesh->MM[k+l*femMesh->Np]*femMesh->Dr[m+k*femMesh->Np];
          femMesh->Sss[m+n*femMesh->Np] += femMesh->Ds[n+l*femMesh->Np]*femMesh->MM[k+l*femMesh->Np]*femMesh->Ds[m+k*femMesh->Np];
        }
      }
    }
  }

  printf("Building full SEMFEM matrix..."); fflush(stdout);

  // Build non-zeros of stiffness matrix (unassembled)
  dlong nnzLocal = femMesh->Np*femMesh->Np*femMesh->Nelements;

  nonZero_t *sendNonZeros = (nonZero_t*) calloc(nnzLocal, sizeof(nonZero_t));
  int *AsendCounts  = (int*) calloc(size, sizeof(int));
  int *ArecvCounts  = (int*) calloc(size, sizeof(int));
  int *AsendOffsets = (int*) calloc(size+1, sizeof(int));
  int *ArecvOffsets = (int*) calloc(size+1, sizeof(int));

  //Build unassembed non-zeros
  dlong cnt =0;
  #pragma omp parallel for
  for (dlong e=0;e<femMesh->Nelements;e++) {
    for (int n=0;n<femMesh->Np;n++) {
      dlong idn = localIds[e*femMesh->Np + n];
      if (pmesh->globalIds[idn]<0) continue; //skip masked nodes
      for (int m=0;m<femMesh->Np;m++) {
        dlong idm = localIds[e*femMesh->Np + m];
        if (pmesh->globalIds[idm]<0) continue; //skip masked nodes

        dfloat val = 0.;

        dfloat Grr = femMesh->ggeo[e*femMesh->Nggeo + G00ID];
        dfloat Grs = femMesh->ggeo[e*femMesh->Nggeo + G01ID];
        dfloat Gss = femMesh->ggeo[e*femMesh->Nggeo + G11ID];
        dfloat J   = femMesh->ggeo[e*femMesh->Nggeo + GWJID];

        val += Grr*femMesh->Srr[m+n*femMesh->Np];
        val += Grs*femMesh->Srs[m+n*femMesh->Np];
        val += Grs*femMesh->Ssr[m+n*femMesh->Np];
        val += Gss*femMesh->Sss[m+n*femMesh->Np];
        val += J*lambda*femMesh->MM[m+n*femMesh->Np];

        dfloat nonZeroThreshold = 1e-7;
        if (fabs(val)>nonZeroThreshold) {
          #pragma omp critical
          {
            // pack non-zero
            sendNonZeros[cnt].val = val;
            sendNonZeros[cnt].row = pmesh->globalIds[idn];
            sendNonZeros[cnt].col = pmesh->globalIds[idm];
            sendNonZeros[cnt].ownerRank = pmesh->globalOwners[idn];
            cnt++;
          }
        }
      }
    }
  }

  // Make the MPI_NONZERO_T data type
  MPI_Datatype MPI_NONZERO_T;
  MPI_Datatype dtype[4] = {MPI_HLONG, MPI_HLONG, MPI_INT, MPI_DFLOAT};
  int blength[4] = {1, 1, 1, 1};
  MPI_Aint addr[4], displ[4];
  MPI_Get_address ( &(sendNonZeros[0]          ), addr+0);
  MPI_Get_address ( &(sendNonZeros[0].col      ), addr+1);
  MPI_Get_address ( &(sendNonZeros[0].ownerRank), addr+2);
  MPI_Get_address ( &(sendNonZeros[0].val      ), addr+3);
  displ[0] = 0;
  displ[1] = addr[1] - addr[0];
  displ[2] = addr[2] - addr[0];
  displ[3] = addr[3] - addr[0];
  MPI_Type_create_struct (4, blength, displ, dtype, &MPI_NONZERO_T);
  MPI_Type_commit (&MPI_NONZERO_T);

  // count how many non-zeros to send to each process
  for(dlong n=0;n<cnt;++n)
    AsendCounts[sendNonZeros[n].ownerRank]++;

  // sort by row ordering
  qsort(sendNonZeros, cnt, sizeof(nonZero_t), parallelCompareRowColumn);

  // find how many nodes to expect (should use sparse version)
  MPI_Alltoall(AsendCounts, 1, MPI_INT, ArecvCounts, 1, MPI_INT, MPI_COMM_WORLD);

  // find send and recv offsets for gather
  dlong nnz = 0;
  for(int r=0;r<size;++r){
    AsendOffsets[r+1] = AsendOffsets[r] + AsendCounts[r];
    ArecvOffsets[r+1] = ArecvOffsets[r] + ArecvCounts[r];
    nnz += ArecvCounts[r];
  }

  nonZero_t *A = (nonZero_t*) calloc(nnz, sizeof(nonZero_t));

  // determine number to receive
  MPI_Alltoallv(sendNonZeros, AsendCounts, AsendOffsets, MPI_NONZERO_T,
                           A, ArecvCounts, ArecvOffsets, MPI_NONZERO_T,
                           MPI_COMM_WORLD);

  // sort received non-zero entries by row block (may need to switch compareRowColumn tests)
  qsort(A, nnz, sizeof(nonZero_t), parallelCompareRowColumn);

  // compress duplicates
  cnt = 0;
  for(dlong n=1;n<nnz;++n){
    if(A[n].row == A[cnt].row && A[n].col == A[cnt].col){
      A[cnt].val += A[n].val;
    } else{
      ++cnt;
      A[cnt] = A[n];
    }
  }
  if (nnz) cnt++;
  nnz = cnt;

  if(rank==0) printf("done.\n");

  MPI_Barrier(MPI_COMM_WORLD);
  MPI_Type_free(&MPI_NONZERO_T);

  hlong *Rows = (hlong *) calloc(nnz, sizeof(hlong));
  hlong *Cols = (hlong *) calloc(nnz, sizeof(hlong));
  dfloat *Vals = (dfloat*) calloc(nnz,sizeof(dfloat));

  for (dlong n=0;n<nnz;n++) {
    Rows[n] = A[n].row;
    Cols[n] = A[n].col;
    Vals[n] = A[n].val;
  }

  precon->parAlmond = parAlmondInit(mesh, parAlmondOptions);
  parAlmondAgmgSetup(precon->parAlmond,
                     globalStarts,
                     nnz,
                     Rows,
                     Cols,
                     Vals,
                     solver->allNeumann,
                     solver->allNeumannPenalty);
  free(A); free(Rows); free(Cols); free(Vals);

  //tell parAlmond not to gather this level (its done manually)
  agmgLevel *baseLevel = precon->parAlmond->levels[0];
  baseLevel->gatherLevel = false;
  baseLevel->weightedInnerProds = false;

  // build interp and anterp
  dfloat *SEMFEMAnterp = (dfloat*) calloc(mesh->NpFEM*mesh->Np, sizeof(dfloat));
  for(int n=0;n<mesh->NpFEM;++n){
    for(int m=0;m<mesh->Np;++m){
      SEMFEMAnterp[n+m*mesh->NpFEM] = mesh->SEMFEMInterp[n*mesh->Np+m];
    }
  }

  mesh->o_SEMFEMInterp = mesh->device.malloc(mesh->NpFEM*mesh->Np*sizeof(dfloat),mesh->SEMFEMInterp);
  mesh->o_SEMFEMAnterp = mesh->device.malloc(mesh->NpFEM*mesh->Np*sizeof(dfloat),SEMFEMAnterp);

  free(SEMFEMAnterp);

  precon->o_rFEM = mesh->device.malloc(mesh->Nelements*mesh->NpFEM*sizeof(dfloat));
  precon->o_zFEM = mesh->device.malloc(mesh->Nelements*mesh->NpFEM*sizeof(dfloat));

  precon->o_GrFEM = mesh->device.malloc(precon->FEMogs->Ngather*sizeof(dfloat));
  precon->o_GzFEM = mesh->device.malloc(precon->FEMogs->Ngather*sizeof(dfloat));
}