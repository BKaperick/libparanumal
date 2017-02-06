#include "acousticsQuad2D.h"

int main(int argc, char **argv){

  // start up MPI
  MPI_Init(&argc, &argv);

  if(argc!=3){
    // to run cavity test case with degree N elements
    printf("usage: ./main meshes/cavityH005.msh N\n");
    exit(-1);
  }

  // int specify polynomial degree 
  int N = atoi(argv[2]);

  // set up mesh stuff
  mesh2D *mesh = meshSetupQuad2D(argv[1], N);

  // set up acoustics stuff
  acousticsSetupQuad2D(mesh);

  // run
  acousticsRunQuad2D(mesh);

  // close down MPI
  MPI_Finalize();

  exit(0);
  return 0;
}
