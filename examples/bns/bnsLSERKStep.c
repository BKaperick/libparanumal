#include "bns.h"

// complete a time step using LSERK4
void bnsLSERKStep(bns_t *bns, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer, setupAide &options){


const dlong offset    = 0.0;
const dlong pmloffset = 0.0;
const int shift       = 0; 
 // bns->shiftIndex = 0; 
 mesh_t *mesh = bns->mesh; 	

// LSERK4 stages
for(int rk=0;rk<mesh->Nrk;++rk){

  // intermediate stage time
  dfloat t = bns->startTime + tstep*bns->dt + bns->dt*mesh->rkc[rk];

  if(mesh->totalHaloPairs>0){
    #if ASYNC 
      mesh->device.setStream(dataStream);
    #endif

    int Nentries = mesh->Np*bns->Nfields;
    mesh->haloExtractKernel(mesh->totalHaloPairs,
                Nentries,
                mesh->o_haloElementList,
                bns->o_q,
                mesh->o_haloBuffer);

    // copy extracted halo to HOST
    mesh->o_haloBuffer.asyncCopyTo(sendBuffer);

    #if ASYNC 
      mesh->device.setStream(defaultStream);
    #endif
  }

    // COMPUTE RAMP FUNCTION 
    dfloat ramp, drampdt;
    bnsRampFunction(t, &ramp, &drampdt);
      

    occaTimerTic(mesh->device, "VolumeKernel");    
    // compute volume contribution to DG boltzmann RHS
    if(mesh->pmlNelements){	
      occaTimerTic(mesh->device,"PmlVolumeKernel");
      bns->pmlVolumeKernel(mesh->pmlNelements,
                        mesh->o_pmlElementIds,
                        mesh->o_pmlIds,
                        offset,    // 0
                        pmloffset, // 0
                        shift, // 0
                        ramp, 
                        drampdt,
                        mesh->o_vgeo,
                        mesh->o_Dmatrices,
                        bns->o_q,
                        bns->o_pmlqx,
                        bns->o_pmlqy,
                        bns->o_pmlqz,
                        bns->o_rhsq,
                        bns->o_pmlrhsqx,
                        bns->o_pmlrhsqy,
                        bns->o_pmlrhsqz);
      occaTimerToc(mesh->device,"PmlVolumeKernel");

    }

    // compute volume contribution to DG boltzmann RHS added d/dt (ramp(qbar)) to RHS
    if(mesh->nonPmlNelements){
      occaTimerTic(mesh->device,"NonPmlVolumeKernel");
       bns->volumeKernel(mesh->nonPmlNelements,
                          mesh->o_nonPmlElementIds,
                          offset, // 0
                          shift, //0
                          ramp, 
                          drampdt,
                          mesh->o_vgeo,
                          mesh->o_Dmatrices,
                          bns->o_q,
                          bns->o_rhsq);
      occaTimerToc(mesh->device,"NonPmlVolumeKernel");
  }
  occaTimerToc(mesh->device, "VolumeKernel");    
    



  occaTimerTic(mesh->device, "RelaxationKernel");
	if(mesh->pmlNelements){
    occaTimerTic(mesh->device, "PmlRelaxationKernel");
	  bns->pmlRelaxationKernel(mesh->pmlNelements,
                              mesh->o_pmlElementIds,
                              mesh->o_pmlIds,
                              offset,    // 0
                              pmloffset, // 0
                              shift, // 0
                              mesh->o_cubInterpT,
                              mesh->o_cubProjectT,
                              bns->o_pmlSigmaX,
                              bns->o_pmlSigmaY,
                              bns->o_pmlSigmaZ,
                              bns->o_q,
                              bns->o_pmlqx,
                              bns->o_pmlqy,
                              bns->o_pmlqz,
                              bns->o_rhsq,
                              bns->o_pmlrhsqx,
                              bns->o_pmlrhsqy,
                              bns->o_pmlrhsqz);
     occaTimerToc(mesh->device, "PmlRelaxationKernel");
	}

	// compute relaxation terms using cubature
	if(mesh->nonPmlNelements){
    occaTimerTic(mesh->device, "NonPmlRelaxationKernel");
	  bns->relaxationKernel(mesh->nonPmlNelements,
                          mesh->o_nonPmlElementIds,
                          offset, // 0
                          shift, //0
                          mesh->o_cubInterpT,
                          mesh->o_cubProjectT,
                          bns->o_q,
                          bns->o_rhsq);  
   occaTimerToc(mesh->device, "NonPmlRelaxationKernel");
	}
	 // VOLUME KERNELS
 occaTimerToc(mesh->device, "RelaxationKernel");

  if(mesh->totalHaloPairs>0){
    
    #if ASYNC 
      mesh->device.setStream(dataStream);
    #endif

    //make sure the async copy is finished
    mesh->device.finish();
    // start halo exchange
    meshHaloExchangeStart(mesh,
                          bns->Nfields*mesh->Np*sizeof(dfloat),
                          sendBuffer,
                          recvBuffer);
    // wait for halo data to arrive
    meshHaloExchangeFinish(mesh);
    // copy halo data to DEVICE
    size_t offset = mesh->Np*bns->Nfields*mesh->Nelements*sizeof(dfloat); // offset for halo data
    bns->o_q.asyncCopyFrom(recvBuffer, haloBytes, offset);
    mesh->device.finish();        

    #if ASYNC 
      mesh->device.setStream(defaultStream);
    #endif
  }



  // SURFACE KERNELS
  occaTimerTic(mesh->device,"SurfaceKernel");

  if(mesh->pmlNelements){
    occaTimerTic(mesh->device,"PmlSurfaceKernel");
    bns->pmlSurfaceKernel(mesh->pmlNelements,
                          mesh->o_pmlElementIds,
                          mesh->o_pmlIds,
                          t,
                          ramp,
                          mesh->o_sgeo,
                          mesh->o_LIFTT,
                          mesh->o_vmapM,
                          mesh->o_vmapP,
                          mesh->o_EToB,
                          mesh->o_x,
                          mesh->o_y,
                          mesh->o_z,
                          bns->o_q,
                          bns->o_rhsq,
                          bns->o_pmlrhsqx,
                          bns->o_pmlrhsqy,
                          bns->o_pmlrhsqz);
    occaTimerToc(mesh->device,"PmlSurfaceKernel");
  }

  if(mesh->nonPmlNelements){
    occaTimerTic(mesh->device,"NonPmlSurfaceKernel");
    bns->surfaceKernel(mesh->nonPmlNelements,
                        mesh->o_nonPmlElementIds,
                        t,
                        ramp,
                        mesh->o_sgeo,
                        mesh->o_LIFTT,
                        mesh->o_vmapM,
                        mesh->o_vmapP,
                        mesh->o_EToB,
                        mesh->o_x,
                        mesh->o_y,
                        mesh->o_z,
                        bns->o_q,
                        bns->o_rhsq);
    occaTimerToc(mesh->device,"NonPmlSurfaceKernel");
  }
  occaTimerToc(mesh->device,"SurfaceKernel");

    
  // ramp function for flow at next RK stage
  dfloat tupdate = tstep*bns->dt + bns->dt*mesh->rkc[rk+1];
  dfloat rampUpdate, drampdtUpdate;
  bnsRampFunction(tupdate, &rampUpdate, &drampdtUpdate);

  //UPDATE
  occaTimerTic(mesh->device,"UpdateKernel");


  //printf("running with %d pml Nelements\n",mesh->pmlNelements);    
  if (mesh->pmlNelements){   
    occaTimerTic(mesh->device,"PmlUpdateKernel");
    bns->pmlUpdateKernel(mesh->pmlNelements,
                          mesh->o_pmlElementIds,
                          mesh->o_pmlIds,
                          bns->dt,
                          mesh->rka[rk],
                          mesh->rkb[rk],
                          rampUpdate,
                          bns->o_rhsq,
                          bns->o_pmlrhsqx,
                          bns->o_pmlrhsqy,
                          bns->o_pmlrhsqz,
                          bns->o_resq,
                          bns->o_pmlresqx,
                          bns->o_pmlresqy,
                          bns->o_pmlresqz,
                          bns->o_pmlqx,
                          bns->o_pmlqy,
                          bns->o_pmlqz,
                          bns->o_q);
    occaTimerToc(mesh->device,"PmlUpdateKernel");

  }

  if(mesh->nonPmlNelements){
    occaTimerTic(mesh->device,"NonPmlUpdateKernel");
    bns->updateKernel(mesh->nonPmlNelements,
                      mesh->o_nonPmlElementIds,
                      bns->dt,
                      mesh->rka[rk],
                      mesh->rkb[rk],
                      bns->o_rhsq,
                      bns->o_resq,
                      bns->o_q);
    occaTimerToc(mesh->device,"NonPmlUpdateKernel");
  }

 occaTimerToc(mesh->device,"UpdateKernel");
    
}


}
