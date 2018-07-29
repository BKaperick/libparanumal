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

#ifndef MESH3D_H 
#define MESH3D_H 1

// generic mesh structure 
#include "mesh.h"

#define mesh3D mesh_t

// mesh readers
mesh3D* meshParallelReaderTet3D(char *fileName);
mesh3D* meshParallelReaderHex3D(char *fileName);

// build connectivity in serial
void meshConnect3D(mesh3D *mesh);

// build element-boundary connectivity
void meshConnectBoundary3D(mesh3D *mesh);

// build connectivity in parallel
void meshParallelConnect3D(mesh3D *mesh);

// repartition elements in parallel
void meshGeometricPartition3D(mesh3D *mesh);

// print out mesh 
void meshPrint3D(mesh3D *mesh);

// print out mesh in parallel from the root process
void meshParallelPrint3D(mesh3D *mesh);

// print out mesh partition in parallel
void meshVTU3D(mesh3D *mesh, char *fileName);

// print out mesh field
void meshPlotVTU3DP(mesh3D *mesh, char *fileNameBase, int fld);

// compute geometric factors for local to physical map 
void meshGeometricFactorsTet3D(mesh3D *mesh);
void meshGeometricFactorsHex3D(mesh3D *mesh);

void meshSurfaceGeometricFactorsTetP3D(mesh3D *mesh);
void meshSurfaceGeometricFactorsHex3D(mesh3D *mesh);

void meshPhysicalNodesTetP3D(mesh3D *mesh);
void meshPhysicalNodesHex3D(mesh3D *mesh);

void meshLoadReferenceNodesTetP3D(mesh3D *mesh, int N);
void meshLoadReferenceNodesHex3D(mesh3D *mesh, int N);

void meshGradientTet3D(mesh3D *mesh, dfloat *q, dfloat *dqdx, dfloat *dqdy, dfloat *dqdz);
void meshGradientHex3D(mesh3D *mesh, dfloat *q, dfloat *dqdx, dfloat *dqdy, dfloat *dqdz);

// print out parallel partition i
void meshPartitionStatistics3D(mesh3D *mesh);

// default occa set up
void meshOccaSetup3D(mesh3D *mesh, char *deviceConfig, occa::kernelInfo &kernelInfo);

// functions that call OCCA kernels
void occaTest3D(mesh3D *mesh, dfloat *q, dfloat *dqdx, dfloat *dqdy, dfloat *dqdz);

// 
void occaOptimizeGradientTet3D(mesh3D *mesh, dfloat *q, dfloat *dqdx, dfloat *dqdy, dfloat *dqdz);
void occaOptimizeGradientHex3D(mesh3D *mesh, dfloat *q, dfloat *dqdx, dfloat *dqdy, dfloat *dqdz);

// serial face-node to face-node connection
void meshConnectFaceNodesP3D(mesh3D *mesh);

//
mesh3D *meshSetupTetP3D(char *filename, int N);
mesh3D *meshSetupHex3D(char *filename, int N);

void meshParallelConnectNodesHex3D(mesh3D *mesh);


// halo connectivity information
void meshHaloSetup3D(mesh3D *mesh);

// perform halo exchange
void meshHaloExchange3D(mesh3D *mesh,
			size_t Nbytes,  // number of bytes per element
			void *sourceBuffer, 
			void *sendBuffer, 
			void *recvBuffer);

void meshHaloExchangeStart3D(mesh3D *mesh,
			     size_t Nbytes,       // message size per element
			     void *sendBuffer,    // temporary buffer
			     void *recvBuffer);

void meshHaloExchangeFinish3D(mesh3D *mesh);

// build list of nodes on each face of the reference element
void meshBuildFaceNodes3D(mesh3D *mesh);
void meshBuildFaceNodesHex3D(mesh3D *mesh);

void meshMRABSetupP3D(mesh3D *mesh, dfloat *EToDT, int maxLevels); 

//MRAB weighted mesh partitioning
void meshMRABWeightedPartitionTetP3D(mesh3D *mesh, dfloat *weights,
                                      int numLevels, int *levels);

void meshSetPolynomialDegree3D(mesh3D *mesh, int N);

#define norm(a,b,c) ( sqrt((a)*(a)+(b)*(b)+(c)*(c)) )

/* offsets for geometric factors */
#define RXID 0  
#define RYID 1  
#define RZID 2
#define SXID 3  
#define SYID 4  
#define SZID 5  
#define TXID 6  
#define TYID 7  
#define TZID 8  
#define  JID 9
#define JWID 10

/* offsets for second order geometric factors */
#define G00ID 0  
#define G01ID 1  
#define G02ID 2
#define G11ID 3  
#define G12ID 4  
#define G22ID 5  
#define GWJID 6  

/* offsets for nx, ny, sJ, 1/J */
#define NXID 0  
#define NYID 1  
#define NZID 2 
#define SJID 3  
#define IJID 4
#define IHID 5
#define WSJID 6
#endif

