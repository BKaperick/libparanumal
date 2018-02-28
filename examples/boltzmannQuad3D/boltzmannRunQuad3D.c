#include "boltzmannQuad3D.h"

void boltzmannRunQuad3D(solver_t *solver){

  //
  mesh_t *mesh = solver->mesh;
  
  // MPI send buffer
  int haloBytes = mesh->totalHaloPairs*mesh->Np*mesh->Nfields*sizeof(dfloat);
  dfloat *sendBuffer = (dfloat*) malloc(haloBytes);
  dfloat *recvBuffer = (dfloat*) malloc(haloBytes);

  int fld = 0;
  boltzmannPlotVTUQuad3D(mesh, "bah.vtu", fld);
  
  occa::initTimer(mesh->device);
  
  // Low storage explicit Runge Kutta (5 stages, 4th order)
  for(int tstep=0;tstep<mesh->NtimeSteps;++tstep){
    
    for(int rk=0;rk<mesh->Nrk;++rk){

      //      printf("RK %d --------------------\n", rk);
      
      // intermediate stage time
      dfloat t = tstep*mesh->dt + mesh->dt*mesh->rkc[rk];

      if(mesh->totalHaloPairs>0){
	// extract halo on DEVICE
	int Nentries = mesh->Np*mesh->Nfields;
	
	mesh->haloExtractKernel(mesh->totalHaloPairs,
				Nentries,
				mesh->o_haloElementList,
				mesh->o_q,
				mesh->o_haloBuffer);
	
	// copy extracted halo to HOST 
	mesh->o_haloBuffer.copyTo(sendBuffer);      
	
	// start halo exchange
	meshHaloExchangeStart(mesh,
			      mesh->Np*mesh->Nfields*sizeof(dfloat),
			      sendBuffer,
			      recvBuffer);
      }


      // compute volume contribution to DG boltzmann RHS
      mesh->volumeKernel(mesh->Nelements,
			 mesh->o_vgeo,
			 mesh->o_D,
			 mesh->o_x,
			 mesh->o_y,
			 mesh->o_z,
			 mesh->o_q,
			 mesh->o_rhsq);
      
      if(mesh->totalHaloPairs>0){
	// wait for halo data to arrive
	meshHaloExchangeFinish(mesh);
	
	// copy halo data to DEVICE
	size_t offset = mesh->Np*mesh->Nfields*mesh->Nelements*sizeof(dfloat); // offset for halo data
	mesh->o_q.copyFrom(recvBuffer, haloBytes, offset);
      }
      
      mesh->device.finish();
      occa::tic("surfaceKernel");

#if 1
      // compute surface contribution to DG boltzmann RHS
      mesh->surfaceKernel(mesh->Nelements,
			  mesh->o_sgeo,
			  mesh->o_LIFTT,
			  mesh->o_vmapM,
			  mesh->o_vmapP,
			  t,
			  mesh->o_x,
			  mesh->o_y,
			  mesh->o_z,
			  mesh->o_q,
			  mesh->o_rhsq);
#endif
      
      mesh->device.finish();
      occa::toc("surfaceKernel");
      
      dfloat tupdate = tstep*mesh->dt + mesh->dt*mesh->rkc[rk+1];

      mesh->device.finish();
      occa::tic("updateKernel");

      mesh->updateKernel(mesh->Nelements,
			 mesh->dt,
			 mesh->rka[rk],
			 mesh->rkb[rk],
			 mesh->o_rhsq,
			 mesh->o_resq,
			 mesh->o_q);

      mesh->device.finish();
      occa::toc("updateKernel");
    }
    
    // estimate maximum error
    if((tstep%mesh->errorStep)==0){
      dfloat t = (tstep+1)*mesh->dt;
      
      printf("tstep = %d, t = %g\n", tstep, t);
      
      // copy data back to host
      mesh->o_q.copyTo(mesh->q);

      // check for nans
      for(int n=0;n<mesh->Nfields*mesh->Nelements*mesh->Np;++n){
	if(isnan(mesh->q[n])){
	  printf("found nan\n");
	  exit(-1);
	}
      }
	  
      
      // do error stuff on host
      //      boltzmannErrorQuad2D(mesh, mesh->dt*(tstep+1));

      // compute vorticity
      //      boltzmannComputeVorticityQuad2D(mesh, mesh->q, 0, mesh->Nfields);
      
      // output field files
      int fld = 0;
      char fname[BUFSIZ];
      //      sprintf(fname, "foo_T%04d.vtu", tstep/mesh->errorStep);

      //      sprintf(fname, "foo_T%04d", tstep/mesh->errorStep);
      boltzmannPlotVTUQuad3DV2(mesh, "foo", tstep/mesh->errorStep);
    }
  }

  occa::printTimer();
  
  free(recvBuffer);
  free(sendBuffer);
}



