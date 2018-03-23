#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "mpi.h"

#include "multiFluidIns2D.h"

// interpolate data to plot nodes and save to file (one per process
void multiFluidInsPlotVTU2D(multiFluidIns_t *solver, char *fileName){

  mesh2D *mesh = solver->mesh;
  
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  FILE *fp;
  
  fp = fopen(fileName, "w");

  fprintf(fp, "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"BigEndian\">\n");
  fprintf(fp, "  <UnstructuredGrid>\n");
  fprintf(fp, "    <Piece NumberOfPoints=\"%d\" NumberOfCells=\"%d\">\n", 
	  mesh->Nelements*mesh->plotNp, 
	  mesh->Nelements*mesh->plotNelements);
  
  // write out nodes
  fprintf(fp, "      <Points>\n");
  fprintf(fp, "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" Format=\"ascii\">\n");
  
  // compute plot node coordinates on the fly
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->plotNp;++n){
      dfloat plotxn = 0, plotyn = 0;

      for(int m=0;m<mesh->Np;++m){
      	plotxn += mesh->plotInterp[n*mesh->Np+m]*mesh->x[m+e*mesh->Np];
      	plotyn += mesh->plotInterp[n*mesh->Np+m]*mesh->y[m+e*mesh->Np];
      }

      fprintf(fp, "       ");
      fprintf(fp, "%g %g %g\n", plotxn,plotyn,0.);
    }
  }
  fprintf(fp, "        </DataArray>\n");
  fprintf(fp, "      </Points>\n");
  

  // write out pressure
  fprintf(fp, "      <PointData Scalars=\"scalars\">\n");


  fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Phi\" Format=\"ascii\">\n");
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->plotNp;++n){
      dfloat plotpn = 0;
      for(int m=0;m<mesh->Np;++m){
      	int id = m+e*mesh->Np;
      	id += solver->index*(mesh->Np)*(mesh->Nelements+mesh->totalHaloPairs);
        dfloat pm = solver->Phi[id];
        plotpn += mesh->plotInterp[n*mesh->Np+m]*pm;
      }

      fprintf(fp, "       ");
      fprintf(fp, "%g\n", plotpn);
    }
  }
  fprintf(fp, "       </DataArray>\n");

  // calculate plot vorticity
  fprintf(fp, "        <DataArray type=\"Float32\" Name=\"VorticityDivergence\" NumberOfComponents=\"2\" Format=\"ascii\">\n");
  dfloat *curlU = (dfloat*) calloc(mesh->Np, sizeof(dfloat));
  dfloat *divU = (dfloat*) calloc(mesh->Np, sizeof(dfloat));
  
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->Np;++n){
      dfloat dUdr = 0, dUds = 0, dVdr = 0, dVds = 0;
      for(int m=0;m<mesh->Np;++m){
	int id = m+e*mesh->Np;
	id += solver->index*(mesh->Np)*(mesh->Nelements+mesh->totalHaloPairs);
	dUdr += mesh->Dr[n*mesh->Np+m]*solver->U[id];
	dUds += mesh->Ds[n*mesh->Np+m]*solver->U[id];
	dVdr += mesh->Dr[n*mesh->Np+m]*solver->V[id];
	dVds += mesh->Ds[n*mesh->Np+m]*solver->V[id];
      }
      dfloat rx = mesh->vgeo[e*mesh->Nvgeo+RXID];
      dfloat ry = mesh->vgeo[e*mesh->Nvgeo+RYID];
      dfloat sx = mesh->vgeo[e*mesh->Nvgeo+SXID];
      dfloat sy = mesh->vgeo[e*mesh->Nvgeo+SYID];

      dfloat dUdx = rx*dUdr + sx*dUds;
      dfloat dUdy = ry*dUdr + sy*dUds;
      dfloat dVdx = rx*dVdr + sx*dVds;
      dfloat dVdy = ry*dVdr + sy*dVds;
      
      curlU[n] = dVdx-dUdy;
      divU[n] = dUdx+dVdy;
    }
    
    for(int n=0;n<mesh->plotNp;++n){
      dfloat plotCurlUn = 0;
      dfloat plotDivUn = 0;
      for(int m=0;m<mesh->Np;++m){
        plotCurlUn += mesh->plotInterp[n*mesh->Np+m]*curlU[m];
        plotDivUn += mesh->plotInterp[n*mesh->Np+m]*divU[m];	
      }
      fprintf(fp, "       ");
      fprintf(fp, "%g %g\n", plotCurlUn, plotDivUn);
    }
  }
  fprintf(fp, "       </DataArray>\n");
  free(curlU);
  free(divU);

  fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Velocity\" NumberOfComponents=\"2\" Format=\"ascii\">\n");
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->plotNp;++n){
      dfloat plotun = 0, plotvn = 0;
      for(int m=0;m<mesh->Np;++m){
      	int id = m+e*mesh->Np;
      	id += solver->index*(mesh->Np)*(mesh->Nelements+mesh->totalHaloPairs);
        dfloat um = solver->U[id];
        dfloat vm = solver->V[id];
        //
        plotun += mesh->plotInterp[n*mesh->Np+m]*um;
        plotvn += mesh->plotInterp[n*mesh->Np+m]*vm;
        
      }
    
      fprintf(fp, "       ");
      fprintf(fp, "%g %g\n", plotun, plotvn);
    }
  }
  fprintf(fp, "       </DataArray>\n");


  // fprintf(fp, "        <DataArray type=\"Float32\" Name=\"Vorticity\" NumberOfComponents=\"3\" Format=\"ascii\">\n");
  // for(int e=0;e<mesh->Nelements;++e){
  //   for(int n=0;n<mesh->plotNp;++n){
  //     dfloat plotwxn = 0, plotwyn = 0, plotvn = 0, plotwzn = 0;
  //     for(int m=0;m<mesh->Np;++m){
  //       dfloat wx = mesh->q[4 + mesh->Nfields*(m+e*mesh->Np)];
  //       dfloat wy = mesh->q[5 + mesh->Nfields*(m+e*mesh->Np)];
  //       dfloat wz = mesh->q[6 + mesh->Nfields*(m+e*mesh->Np)];
  //       //
  //       plotwxn += mesh->plotInterp[n*mesh->Np+m]*wx;
  //       plotwyn += mesh->plotInterp[n*mesh->Np+m]*wy;
  //       plotwzn += mesh->plotInterp[n*mesh->Np+m]*wz;
  //     }
      
  //     fprintf(fp, "       ");
  //     fprintf(fp, "%g %g %g\n", plotwxn, plotwyn,plotwzn);
  //   }
  // }
  // fprintf(fp, "       </DataArray>\n");



  fprintf(fp, "     </PointData>\n");
  
  fprintf(fp, "    <Cells>\n");
  fprintf(fp, "      <DataArray type=\"Int32\" Name=\"connectivity\" Format=\"ascii\">\n");
  
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->plotNelements;++n){
      fprintf(fp, "       ");
      for(int m=0;m<mesh->plotNverts;++m){
	fprintf(fp, "%d ", e*mesh->plotNp + mesh->plotEToV[n*mesh->plotNverts+m]);
      }
      fprintf(fp, "\n");
    }
  }
  
  fprintf(fp, "        </DataArray>\n");
  
  fprintf(fp, "        <DataArray type=\"Int32\" Name=\"offsets\" Format=\"ascii\">\n");
  int cnt = 0;
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->plotNelements;++n){
      cnt += mesh->plotNverts;
      fprintf(fp, "       ");
      fprintf(fp, "%d\n", cnt);
    }
  }
  fprintf(fp, "       </DataArray>\n");
  
  fprintf(fp, "       <DataArray type=\"Int32\" Name=\"types\" Format=\"ascii\">\n");
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->plotNelements;++n){
      fprintf(fp, "5\n");
    }
  }
  fprintf(fp, "        </DataArray>\n");
  fprintf(fp, "      </Cells>\n");
  fprintf(fp, "    </Piece>\n");
  fprintf(fp, "  </UnstructuredGrid>\n");
  fprintf(fp, "</VTKFile>\n");
  fclose(fp);

}
