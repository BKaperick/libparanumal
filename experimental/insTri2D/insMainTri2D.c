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

#include "insTri2D.h"

int main(int argc, char **argv){

  // start up MPI
  MPI_Init(&argc, &argv);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  // SET OPTIONS
  // out  = REPORT, REPORT+VTU
  // adv  = CUBATURE, COLLOCATION
  //int Ns = 0; // no-subcycling 
  int Ns = 4; 
  if(argc==5)
    Ns = atoi(argv[4]); // Number of substeps
  char *options; 
  if(Ns==0)
    options = strdup("out=VTU, adv=CUBATURE");
  else
    options = strdup("out=VTU, adv=CUBATURE,SUBCYCLING");  //pres=PRESSURE_HISTORY


  char *velSolverOptions =
    strdup("solver=PCG method=IPDG basis=NODAL preconditioner=MASSMATRIX");
  char *velParAlmondOptions =
    strdup("solver= smoother= partition=");

  char *prSolverOptions =
    strdup("solver=PCG,FLEXIBLE method=CONTINUOUS basis=NODAL preconditioner=MULTIGRID,HALFDOFS smoother=DAMPEDJACOBI,CHEBYSHEV");
    //strdup("solver=PCG,FLEXIBLE, method=CONTINUOUS basis=NODAL preconditioner=FULLALMOND");

  char *prParAlmondOptions =
    strdup("solver=KCYCLE smoother=CHEBYSHEV partition=STRONGNODES");

  if(argc!=3 && argc!=4 && argc!=5){
    printf("usage 1: ./main meshes/cavityH005.msh N\n");
    printf("usage 2: ./main meshes/cavityH005.msh N insUniformFlowBoundaryConditions.h\n");
    printf("usage 3: ./main meshes/cavityH005.msh N insUniformFlowBoundaryConditions.h Nsubstep\n");
    exit(-1);
  }
  // int specify polynomial degree
  int N = atoi(argv[2]);

  // set up mesh stuff
  mesh2D *mesh = meshSetupTri2D(argv[1], N); 

  // capture header file
  char *boundaryHeaderFileName;
  if(argc==3)
    boundaryHeaderFileName = strdup(DHOLMES "/examples/insTri2D/insUniform2D.h"); // default
  else
    boundaryHeaderFileName = strdup(argv[3]);

  if (rank==0) printf("Setup INS Solver: \n");
  ins_t *ins = insSetupTri2D(mesh,Ns,options, velSolverOptions,   velParAlmondOptions,
                          prSolverOptions, prParAlmondOptions, boundaryHeaderFileName);
  if (rank==0) printf("Running INS solver\n");

  insRunTri2D(ins,options);

  // close down MPI
  MPI_Finalize();

  exit(0);
  return 0;
}
