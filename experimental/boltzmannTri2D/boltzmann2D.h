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
#include <math.h>
#include <mpi.h>
#include "mesh2D.h"
#include <complex.h>  
#include "mesh2D.h"


#define blockSize 256

typedef struct{

	int Nfields; // Number of fields
	int Nrhs;   // Number of RHS
	int Nimex;  // IMEX order
	
	int Ntscale; // Dummy remove later!!!
	dfloat maxError; 

	int totalElements; 
	int Nblock;
	int NrkStages; 
	int frame; 

	dfloat dt, cdt, dtMIN;    // time step
	dfloat startTime ; // Start Time
	dfloat finalTime;  // final time 
	dfloat Lambda2;    // Penalty flux
	dfloat cfl; 

	int NtimeSteps;  // number of time steps
	int Nrk;
	int shiftIndex;    // Rhs index shifting for time steppers

	int probeFlag; 
	int errorFlag;
	int reportFlag;
	int pmlFlag;
	int errorStep;   // number of steps between error calculations
	int reportStep;  // number of steps between error calculations

	dfloat RT, sqrtRT, tauInv, Ma, Re; // Flow parameters


	mesh_t *mesh; 

	dfloat *q, *rhsq, *resq;
   
    int tmethod; 

	// IMEXRK - Kennedy-Carpanter
	dfloat *rhsqim, *rhsqex, *rkrhsqim, *rkrhsqex; 


	dfloat *pmlSigma, *pmlSigmaX, *pmlSigmaY; 
	dfloat *pmlqx, *pmlqy;
	dfloat *pmlrhsqx, *pmlrhsqy;
	dfloat *pmlresqx, *pmlresqy;

	// IMEX Coefficients
	dfloat LSIMEX_B[4], LSIMEX_C[4], LSIMEX_ABi[4], LSIMEX_ABe[4], LSIMEX_Ad[4];
	// MRSAAB Coefficients
	dfloat *MRSAAB_A, *MRSAAB_B, *MRSAAB_C, *MRAB_A, *MRAB_B, *MRAB_C;
	// SARK and RK3 Coefficients
	dfloat RK_A[5][5], RK_B[5], RK_C[5], SARK_A[5][5], SARK_B[5], SARK_C[5]; 
	// DOPRI45
	// Change this later this shouldnt be hard-coded
	// dfloat rkC[7], rkA[7*7], rkE[7];
	// dfloat rkC[5], rkA[5*5], rkE[5];

	dfloat *rkC, *rkA, *rkE; 
    
    // IMEXRK
	dfloat *rkCex, *rkAex, *rkBex, *rkEex;
	dfloat *rkCim, *rkAim, *rkBim, *rkEim;

	int emethod; 
	int tstep, atstep, rtstep, tstepAccepted, rkp;
	dfloat ATOL, RTOL, time; 
	dfloat *ehist, *dthist; 

	dfloat outputInterval, nextOutputTime;      

    // dfloat sarkC[5], sarkA[5*5], sarkE[5];


	dfloat *rkq, *rkrhsq, *rkerr, *errtmp;
	dfloat *rkqx, *rkrhsqx;
	dfloat *rkqy, *rkrhsqy;

	occa::memory o_rkq, o_rkrhsq, o_rkerr;
	occa::memory o_errtmp;

	// IMEXRK 
	occa::memory o_rhsqim, o_rhsqex, o_rkrhsqim, o_rkrhsqex;
	occa::memory o_rkAex, o_rkEex, o_rkBex;  
	occa::memory o_rkAim, o_rkEim, o_rkBim;  





	dfloat *fQM; 
	occa::memory o_q,o_rhsq, o_resq, o_fQM;
	occa::memory o_rkA, o_rkE, o_sarkC, o_sarkA, o_sarkE; 
	occa::memory o_rkqx, o_rkqy, o_rkrhsqx, o_rkrhsqy; 
	occa::memory o_saveq, o_saveqx, o_saveqy; // for output minor step of addaptive RK 

	// LS Imex vars
	occa::memory o_qY,   o_qZ,   o_qS;
	occa::memory o_qYx,  o_qZx,  o_qSx;
	occa::memory o_qYy,  o_qZy,  o_qSy;

	occa::memory o_pmlSigmaX, o_pmlSigmaY;
	occa::memory o_pmlqx, o_pmlqy; 
	occa::memory o_pmlrhsqx, o_pmlrhsqy;
	occa::memory o_pmlresqx, o_pmlresqy;

	occa::kernel volumeKernel;
	occa::kernel surfaceKernel;
	occa::kernel updateKernel;
	occa::kernel traceUpdateKernel;
  occa::kernel relaxationKernel;

  occa::kernel pmlVolumeKernel;
	occa::kernel pmlSurfaceKernel;
	occa::kernel pmlRelaxationKernel;
	occa::kernel pmlUpdateKernel;
	occa::kernel pmlTraceUpdateKernel;

	// Boltzmann Imex Kernels
	occa::kernel implicitUpdateKernel;
	occa::kernel pmlImplicitUpdateKernel;
	occa::kernel implicitSolveKernel;
	occa::kernel pmlImplicitSolveKernel;
	//
	occa::kernel residualUpdateKernel;
	occa::kernel pmlResidualUpdateKernel;

	occa::kernel updateStageKernel;
	occa::kernel pmlUpdateStageKernel;

	occa::kernel errorEstimateKernel;

	// IMEXRK Damping Terms
	occa::kernel pmlDampingKernel; 



//Boltzmann DOPRI5, later move to boltzmann class
// dfloat *rkq, *rkrhsq, *rkerr;
// dfloat *errtmp;
// dfloat rkC[7], rkA[7*7], rkE[7];


// occa::memory o_rkq, o_rkrhsq, o_rkerr;
// occa::memory o_errtmp;
// occa::memory o_rkA, o_rkE;


}bns_t;


bns_t *boltzmannSetup2D(mesh2D *mesh, setupAide &options);


void boltzmannRun2D(bns_t *bns, setupAide &options);
void boltzmannReport2D(bns_t *bns, int tstep, setupAide &options);
void boltzmannError2D(bns_t *bns, int tstep, setupAide &options);
void boltzmannForces2D(bns_t *bns, dfloat time, setupAide &options);

void boltzmannMRABPmlSetup2D(bns_t *bns, setupAide &options);
void boltzmannPmlSetup2D(bns_t *bns, setupAide &options);
void boltzmannTimeStepperCoefficients(bns_t *bns, setupAide &options);
void boltzmannSAADRKCoefficients(bns_t *bns, setupAide &options);


void boltzmannReportAddaptive2D(bns_t *bns, dfloat time, setupAide &options);




void boltzmannPlotVTU2D(bns_t *bns, char * FileName);
void boltzmannPlotTEC2D(bns_t *bns, char * FileName, dfloat solutionTime);


// Time Discretizations one step LSERK
void boltzmannLSERKStep2D(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
// Time Discretizations one step 
void boltzmannSAABStep2D(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
// Execute one Boltzmann time step using LSIMEX
void boltzmannLSIMEXStep2D(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
//Execute one Boltzmann time step using SARK
void boltzmannSARKStep2D(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
// Time Discretizations one step LSERK
void boltzmannSRABStep2D(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
// Execute one Boltzmann time step using MRAB
void boltzmannMRABStep2D(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
// Execute one Boltzmann time step using MRSAAB
void boltzmannMRSAABStep2D(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
// DOPRI Step
void boltzmannDOPRIStep2D(bns_t *bns, dfloat time, int haloBytes, 
					dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options);
// XDOPRI Step
void boltzmannXDOPRIStep2D(bns_t *bns, dfloat time, int haloBytes, 
					dfloat *sendBuffer, dfloat *recvBuffer, setupAide &options);

void boltzmannSAADRKStep2D(bns_t *bns, dfloat time, int haloBytes,
					dfloat *sendBuffer, dfloat *recvBuffer, setupAide &options);

void boltzmannIMEXRKStep2D(bns_t *bns, dfloat time, int haloBytes,
					dfloat *sendBuffer, dfloat *recvBuffer, setupAide &options);

// Embedded Run
void boltzmannRunEmbedded2D(bns_t *bns, int haloBytes, dfloat * sendBuffer, 
	              dfloat *recvBuffer, setupAide &options);


void boltzmannErrorControl2D(bns_t *bns, setupAide &options);


// Needs to be changed if in use
void boltzmannPeriodic2D(mesh2D *mesh, dfloat xper, dfloat yper);
void boltzmannCouetteError2D(mesh2D *mesh, dfloat time);

void boltzmannPlotVTUField2D(mesh2D *mesh, char *fname);
void boltzmannPlotFieldTEC2D(mesh2D *mesh, char * FileName, dfloat solutionTime, int fld);
void boltzmannComputeVorticity2D(mesh2D *mesh, dfloat *q, int outfld, int Nfields);

//dfloat boltzmannRampFunction2D(dfloat t);
void boltzmannRampFunction2D(dfloat t, dfloat *ramp, dfloat *drampdt);






