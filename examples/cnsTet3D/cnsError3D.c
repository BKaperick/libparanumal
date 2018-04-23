#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include "mesh3D.h"


void cnsError3D(mesh3D *mesh, dfloat time){

  dfloat maxR = 0;
  dfloat minR = 1E9;
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->Np;++n){
      dfloat u,v,w, p;
      int id = n+e*mesh->Np;
      dfloat x = mesh->x[id];
      dfloat y = mesh->y[id];
      dfloat z = mesh->z[id];

      int qbase = n+e*mesh->Np*mesh->Nfields;
      maxR = mymax(maxR, mesh->q[qbase]);
      minR = mymin(minR, mesh->q[qbase]);
    }
  }

  // compute maximum over all processes
  dfloat globalMaxR;
  dfloat globalMinR;
  MPI_Allreduce(&maxR, &globalMaxR, 1, MPI_DFLOAT, MPI_MAX, MPI_COMM_WORLD);
  MPI_Allreduce(&minR, &globalMinR, 1, MPI_DFLOAT, MPI_MIN, MPI_COMM_WORLD);

  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  if(rank==0)
    printf("%g, %g, %g ( time, min density, max density)\n", time, globalMinR, globalMaxR);
  
}
