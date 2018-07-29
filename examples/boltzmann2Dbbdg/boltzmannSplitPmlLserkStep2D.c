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

#include "boltzmann2D.h"

// complete a time step using LSERK4
void boltzmannSplitPmlLserkStep2D(mesh2D *mesh, int tstep, int haloBytes,
				  dfloat * sendBuffer, dfloat *recvBuffer){

  // LSERK4 stages
  for(int rk=0;rk<mesh->Nrk;++rk){

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
      meshHaloExchangeStart2D(mesh,
			      mesh->Np*mesh->Nfields*sizeof(dfloat),
			      sendBuffer,
			      recvBuffer);
    }
    
    dfloat ramp, drampdt;
    boltzmannRampFunction2D(t, &ramp, &drampdt);
    
    mesh->device.finish();
    occa::tic("volumeKernel");
    
    // compute volume contribution to DG boltzmann RHS
    if (mesh->pmlNelements){
      mesh->pmlVolumeKernel(mesh->pmlNelements,
			    mesh->o_pmlElementIds,
			    mesh->o_vgeo,
			    mesh->o_sigmax,
			    mesh->o_sigmay,
			    mesh->o_DrT,
			    mesh->o_DsT,
			    mesh->o_q,
			    mesh->o_pmlqx,
			    mesh->o_pmlqy,
			    mesh->o_pmlNT,
			    mesh->o_rhspmlqx,
			    mesh->o_rhspmlqy,
			    mesh->o_rhspmlNT);
    }
    
    // compute volume contribution to DG boltzmann RHS
    // added d/dt (ramp(qbar)) to RHS
    if(mesh->nonPmlNelements){
#if 0
      mesh->volumeKernel(mesh->nonPmlNelements,
			 mesh->o_nonPmlElementIds,
			 ramp, 
			 drampdt,
			 mesh->o_vgeo,
			 mesh->o_DrT,
			 mesh->o_DsT,
			 mesh->o_q,
			 mesh->o_rhsq);
#else // bbdg version
      mesh->volumeKernel(mesh->nonPmlNelements,
			 mesh->o_nonPmlElementIds,
			 ramp, 
			 drampdt,
			 mesh->o_vgeo,
			 mesh->o_D1ids,
			 mesh->o_D2ids,
			 mesh->o_D3ids,
			 mesh->o_Dvals,
			 mesh->o_q,
			 mesh->o_rhsq);
#endif
    }
    
    mesh->device.finish();
    occa::toc("volumeKernel");
      
#if 1
    // compute relaxation terms using cubature
    mesh->relaxationKernel(mesh->pmlNelements,
			   mesh->o_pmlElementIds,
			   mesh->o_VBq,
			   mesh->o_PBq,			   
			   //			   mesh->o_cubInterpT,
			   //			   mesh->o_cubProjectT,
			   mesh->o_q,
			   mesh->o_rhspmlqx,
			   mesh->o_rhspmlqy,
			   mesh->o_rhspmlNT);
#endif

    // complete halo exchange
    if(mesh->totalHaloPairs>0){
      // wait for halo data to arrive
      meshHaloExchangeFinish2D(mesh);
      
      // copy halo data to DEVICE
      size_t offset = mesh->Np*mesh->Nfields*mesh->Nelements*sizeof(dfloat); // offset for halo data
      mesh->o_q.copyFrom(recvBuffer, haloBytes, offset);
    }
    
    mesh->device.finish();
    occa::tic("surfaceKernel");
    
    // compute surface contribution to DG boltzmann RHS
    if (mesh->pmlNelements){
      mesh->pmlSurfaceKernel(mesh->pmlNelements,
			     mesh->o_pmlElementIds,
			     mesh->o_sgeo,
			     mesh->o_LIFTT,
			     mesh->o_vmapM,
			     mesh->o_vmapP,
			     mesh->o_EToB,
			     t,
			     mesh->o_x,
			     mesh->o_y,
			     ramp,
			     mesh->o_q,
			     mesh->o_rhspmlqx,
			     mesh->o_rhspmlqy);
    }
    
    if(mesh->nonPmlNelements){
#if 0
      mesh->surfaceKernel(mesh->nonPmlNelements,
			  mesh->o_nonPmlElementIds,
			  mesh->o_sgeo,
			  mesh->o_LIFTT,
			  mesh->o_vmapM,
			  mesh->o_vmapP,
			  mesh->o_EToB,
			  t,
			  mesh->o_x,
			  mesh->o_y,
			  ramp,
			  mesh->o_q,
			  mesh->o_rhsq);
#else // bbdg version
      mesh->surfaceKernel(mesh->nonPmlNelements,
			  mesh->o_nonPmlElementIds,
			  mesh->o_sgeo,
			  mesh->o_L0vals,
			  mesh->o_ELids,
			  mesh->o_ELvals,			  
			  mesh->o_vmapM,
			  mesh->o_vmapP,
			  mesh->o_EToB,
			  t,
			  mesh->o_x,
			  mesh->o_y,
			  ramp,
			  mesh->o_q,
			  mesh->o_rhsq);
#endif
    }
    mesh->device.finish();
    occa::toc("surfaceKernel");
    
    // ramp function for flow at next RK stage
    dfloat tupdate = tstep*mesh->dt + mesh->dt*mesh->rkc[rk+1];
    dfloat rampUpdate, drampdtUpdate;
    boltzmannRampFunction2D(tupdate, &rampUpdate, &drampdtUpdate);
    
    mesh->device.finish();
    occa::tic("updateKernel");
    
    //printf("running with %d pml Nelements\n",mesh->pmlNelements);    
    if (mesh->pmlNelements){    
      mesh->pmlUpdateKernel(mesh->pmlNelements,
			    mesh->o_pmlElementIds,
			    mesh->dt,
			    mesh->rka[rk],
			    mesh->rkb[rk],
			    rampUpdate,
			    mesh->o_rhspmlqx,
			    mesh->o_rhspmlqy,
			    mesh->o_rhspmlNT,
			    mesh->o_respmlqx,
			    mesh->o_respmlqy,
			    mesh->o_respmlNT,
			    mesh->o_pmlqx,
			    mesh->o_pmlqy,
			    mesh->o_pmlNT,
			    mesh->o_q);
    }
    
    if(mesh->nonPmlNelements)
      mesh->updateKernel(mesh->nonPmlNelements,
			 mesh->o_nonPmlElementIds,
			 mesh->dt,
			 mesh->rka[rk],
			 mesh->rkb[rk],
			 mesh->o_rhsq,
			 mesh->o_resq,
			 mesh->o_q);
    
    mesh->device.finish();
    occa::toc("updateKernel");      
    
  }
}
