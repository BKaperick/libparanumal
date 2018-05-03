#ifndef ELLIPTIC_H
#define ELLIPTIC_H 1

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mpi.h"
#include "mesh2D.h"
#include "mesh3D.h"
#include "parAlmond.h"
#include "ellipticPrecon.h"

// block size for reduction (hard coded)
#define blockSize 256

typedef struct {

  int dim;
  int elementType; // number of edges (3=tri, 4=quad, 6=tet, 12=hex)

  mesh_t *mesh;

  precon_t *precon;

  ogs_t *ogs;

  setupAide options;

  char *type;

  dlong Nblock;

  dfloat tau;

  int *BCType;

  bool allNeumann;
  dfloat allNeumannPenalty;
  dfloat allNeumannScale;

  // HOST shadow copies
  dfloat *x, *Ax, *p, *r, *z, *Ap, *tmp, *grad;
  dfloat *invDegree;

  dfloat *Ry, *R; //multigrid restriction matrix

  int *EToB;

  //C0-FEM mask data
  int *mapB;      // boundary flag of face nodes
  dlong Nmasked;
  dlong *maskIds;

  occa::memory o_maskIds;
  occa::memory o_mapB;

  dfloat *sendBuffer, *recvBuffer;
  dfloat *gradSendBuffer, *gradRecvBuffer;

  occa::stream defaultStream;
  occa::stream dataStream;

  occa::memory o_x;
  occa::memory o_r;
  occa::memory o_p; // search direction
  occa::memory o_z; // preconditioner solution
  occa::memory o_res;
  occa::memory o_Sres;
  occa::memory o_Ax; // A*initial guess
  occa::memory o_Ap; // A*search direction
  occa::memory o_tmp; // temporary
  occa::memory o_grad; // temporary gradient storage (part of A*)
  occa::memory o_rtmp;
  occa::memory o_invDegree;
  occa::memory o_EToB;
  occa::memory o_R;
  occa::memory o_Ry;

  occa::memory o_Dmatrices;
  occa::memory o_Smatrices;

  // list of elements that are needed for global gather-scatter
  dlong NglobalGatherElements;
  occa::memory o_globalGatherElementList;

  // list of elements that are not needed for global gather-scatter
  dlong NlocalGatherElements;
  occa::memory o_localGatherElementList;

  occa::kernel AxKernel;
  occa::kernel partialAxKernel;
  occa::kernel rhsBCKernel;
  occa::kernel addBCKernel;
  occa::kernel innerProductKernel;
  occa::kernel weightedInnerProduct1Kernel;
  occa::kernel weightedInnerProduct2Kernel;
  occa::kernel scaledAddKernel;
  occa::kernel dotMultiplyKernel;
  occa::kernel dotDivideKernel;

  occa::kernel gradientKernel;
  occa::kernel ipdgKernel;
  occa::kernel partialGradientKernel;
  occa::kernel partialIpdgKernel;
  occa::kernel rhsBCIpdgKernel;

}elliptic_t;

elliptic_t *ellipticSetup(mesh2D *mesh, dfloat lambda, occa::kernelInfo &kernelInfo, setupAide options);

void ellipticParallelGatherScatter(mesh2D *mesh, ogs_t *ogs, occa::memory &o_v, const char *type, const char *op);
void ellipticParallelGatherScatterSetup(elliptic_t *elliptic);

void ellipticPreconditioner(elliptic_t *elliptic, dfloat lambda, occa::memory &o_r, occa::memory &o_z);
void ellipticPreconditionerSetup(elliptic_t *elliptic, ogs_t *ogs, dfloat lambda);

int  ellipticSolve(elliptic_t *elliptic, dfloat lambda, dfloat tol, occa::memory &o_r, occa::memory &o_x);
void ellipticSolveSetup(elliptic_t *elliptic, dfloat lambda, occa::kernelInfo &kernelInfo);


void ellipticStartHaloExchange(elliptic_t *elliptic, occa::memory &o_q, int Nentries, dfloat *sendBuffer, dfloat *recvBuffer);
void ellipticInterimHaloExchange(elliptic_t *elliptic, occa::memory &o_q, int Nentries, dfloat *sendBuffer, dfloat *recvBuffer);
void ellipticEndHaloExchange(elliptic_t *elliptic, occa::memory &o_q, int Nentries, dfloat *recvBuffer);

//Linear solvers
int pcg      (elliptic_t* elliptic, dfloat lambda, occa::memory &o_r, occa::memory &o_x, const dfloat tol, const int MAXIT);

void ellipticScaledAdd(elliptic_t *elliptic, dfloat alpha, occa::memory &o_a, dfloat beta, occa::memory &o_b);
dfloat ellipticWeightedInnerProduct(elliptic_t *elliptic, occa::memory &o_w, occa::memory &o_a, occa::memory &o_b);

void ellipticOperator(elliptic_t *elliptic, dfloat lambda, occa::memory &o_q, occa::memory &o_Aq);


void ellipticBuildIpdg(elliptic_t* elliptic, int basisNp, dfloat *basis, dfloat lambda, 
                        nonZero_t **A, dlong *nnzA, hlong *globalStarts);

void ellipticBuildContinuous(elliptic_t* elliptic, dfloat lambda, nonZero_t **A, 
                                  dlong *nnz, ogs_t **ogs, hlong *globalStarts);

void ellipticBuildJacobi(elliptic_t *elliptic, dfloat lambda, dfloat **invDiagA);

void ellipticBuildLocalPatches(elliptic_t *elliptic, dfloat lambda, dfloat rateTolerance,
                               dlong *Npataches, dlong **patchesIndex, dfloat **patchesInvA);

//smoother setups
void ellipticSetupSmoother(elliptic_t *elliptic, precon_t *precon, dfloat lambda);
void ellipticSetupSmootherDampedJacobi    (elliptic_t *elliptic, precon_t *precon, agmgLevel *level, dfloat lambda);
void ellipticSetupSmootherLocalPatch(elliptic_t *elliptic, precon_t *precon, agmgLevel *level, dfloat lambda, dfloat rateTolerance);

void ellipticMultiGridSetup(elliptic_t *elliptic, precon_t* precon, dfloat lambda);
elliptic_t *ellipticBuildMultigridLevel(elliptic_t *baseElliptic, int Nc, int Nf);

void ellipticSEMFEMSetup(elliptic_t *elliptic, precon_t* precon, dfloat lambda);

dfloat maxEigSmoothAx(elliptic_t* elliptic, agmgLevel *level);

#endif
