#include "elliptic.h"

void ellipticOperator(elliptic_t *elliptic, dfloat lambda, occa::memory &o_q, occa::memory &o_Aq, const char *precision){

  mesh_t *mesh = elliptic->mesh;
  setupAide options = elliptic->options;

  occaTimerTic(mesh->device,"AxKernel");

  dfloat *sendBuffer = elliptic->sendBuffer;
  dfloat *recvBuffer = elliptic->recvBuffer;
  dfloat *gradSendBuffer = elliptic->gradSendBuffer;
  dfloat *gradRecvBuffer = elliptic->gradRecvBuffer;

  dfloat alpha = 0., alphaG = 0.;
  dlong Nblock = elliptic->Nblock;
  dfloat *tmp = elliptic->tmp;
  occa::memory &o_tmp = elliptic->o_tmp;

  int one = 1;
  dlong dOne = 1;

  if(options.compareArgs("DISCRETIZATION", "CONTINUOUS")){
    ogs_t *ogs = elliptic->mesh->ogs;

    int mapType = (elliptic->elementType==HEXAHEDRA &&
		   options.compareArgs("ELEMENT MAP", "TRILINEAR")) ? 1:0;

    occa::kernel &partialAxKernel = (strstr(precision, "float")) ? elliptic->partialFloatAxKernel : elliptic->partialAxKernel;
    
    if(elliptic->NglobalGatherElements) {
      if(mapType==0)
	partialAxKernel(elliptic->NglobalGatherElements, elliptic->o_globalGatherElementList,
			mesh->o_ggeo, mesh->o_Dmatrices, mesh->o_Smatrices, mesh->o_MM, lambda, o_q, o_Aq);
      else
	partialAxKernel(elliptic->NglobalGatherElements, elliptic->o_globalGatherElementList,
			elliptic->o_EXYZ, elliptic->o_gllzw, mesh->o_Dmatrices, mesh->o_Smatrices, mesh->o_MM, lambda, o_q, o_Aq);
    }
    if(ogs->NhaloGather) {
      mesh->device.finish();
      mesh->device.setStream(elliptic->dataStream);

      mesh->gatherKernel(ogs->NhaloGather, ogs->o_haloGatherOffsets, ogs->o_haloGatherLocalIds, one, dOne, o_Aq, ogs->o_haloGatherTmp);
      ogs->o_haloGatherTmp.copyTo(ogs->haloGatherTmp,"async: true");

      mesh->device.setStream(elliptic->defaultStream);
    }

    if(elliptic->NlocalGatherElements){
      if(mapType==0)
	partialAxKernel(elliptic->NlocalGatherElements, elliptic->o_localGatherElementList,
				  mesh->o_ggeo, mesh->o_Dmatrices, mesh->o_Smatrices, mesh->o_MM, lambda, o_q, o_Aq);
      else
	partialAxKernel(elliptic->NlocalGatherElements, elliptic->o_localGatherElementList,
			elliptic->o_EXYZ, elliptic->o_gllzw, mesh->o_Dmatrices, mesh->o_Smatrices, mesh->o_MM, lambda, o_q, o_Aq);
    }
    
    // finalize gather using local and global contributions
    if(ogs->NnonHaloGather) 
      mesh->gatherScatterKernel(ogs->NnonHaloGather, ogs->o_nonHaloGatherOffsets, ogs->o_nonHaloGatherLocalIds, one, dOne, o_Aq);

    // C0 halo gather-scatter (on data stream)
    if(ogs->NhaloGather) {
      mesh->device.setStream(elliptic->dataStream);
      mesh->device.finish();

      // MPI based gather scatter using libgs
      gsParallelGatherScatter(ogs->haloGsh, ogs->haloGatherTmp, dfloatString, "add");

      // copy totally gather halo data back from HOST to DEVICE
      ogs->o_haloGatherTmp.copyFrom(ogs->haloGatherTmp,"async: true");
    
      // do scatter back to local nodes
      mesh->scatterKernel(ogs->NhaloGather, ogs->o_haloGatherOffsets, ogs->o_haloGatherLocalIds, one, dOne, ogs->o_haloGatherTmp, o_Aq);
    
      mesh->device.finish(); 
      mesh->device.setStream(elliptic->defaultStream);
    }

    if(elliptic->allNeumann) {
      // mesh->sumKernel(mesh->Nelements*mesh->Np, o_q, o_tmp);
      elliptic->innerProductKernel(mesh->Nelements*mesh->Np, elliptic->o_invDegree, o_q, o_tmp);
      o_tmp.copyTo(tmp);

      for(dlong n=0;n<Nblock;++n)
        alpha += tmp[n];

      MPI_Allreduce(&alpha, &alphaG, 1, MPI_DFLOAT, MPI_SUM, mesh->comm);
      alphaG *= elliptic->allNeumannPenalty*elliptic->allNeumannScale*elliptic->allNeumannScale;

      mesh->addScalarKernel(mesh->Nelements*mesh->Np, alphaG, o_Aq);
    }

    //post-mask
    if (elliptic->Nmasked) 
      mesh->maskKernel(elliptic->Nmasked, elliptic->o_maskIds, o_Aq);

  } else if(options.compareArgs("DISCRETIZATION", "IPDG")) {
    dlong offset = 0;
    dfloat alpha = 0., alphaG =0.;
    dlong Nblock = elliptic->Nblock;
    dfloat *tmp = elliptic->tmp;
    occa::memory &o_tmp = elliptic->o_tmp;

    ellipticStartHaloExchange(elliptic, o_q, mesh->Np, sendBuffer, recvBuffer);

    if(options.compareArgs("BASIS", "NODAL")) {
      elliptic->partialGradientKernel(mesh->Nelements,
          offset,
          mesh->o_vgeo,
          mesh->o_Dmatrices,
          o_q,
          elliptic->o_grad);
    } else if(options.compareArgs("BASIS", "BERN")) {
      elliptic->partialGradientKernel(mesh->Nelements,
          offset,
          mesh->o_vgeo,
          mesh->o_D1ids,
          mesh->o_D2ids,
          mesh->o_D3ids,
          mesh->o_Dvals,
          o_q,
          elliptic->o_grad);
    }

    ellipticInterimHaloExchange(elliptic, o_q, mesh->Np, sendBuffer, recvBuffer);

    //Start the rank 1 augmentation if all BCs are Neumann
    //TODO this could probably be moved inside the Ax kernel for better performance
    if(elliptic->allNeumann)
      mesh->sumKernel(mesh->Nelements*mesh->Np, o_q, o_tmp);

    if(mesh->NinternalElements) {
      if(options.compareArgs("BASIS", "NODAL")) {
        elliptic->partialIpdgKernel(mesh->NinternalElements,
            mesh->o_internalElementIds,
            mesh->o_vmapM,
            mesh->o_vmapP,
            lambda,
            elliptic->tau,
            mesh->o_vgeo,
            mesh->o_sgeo,
            elliptic->o_EToB,
            mesh->o_Dmatrices,
            mesh->o_LIFTT,
            mesh->o_MM,
            elliptic->o_grad,
            o_Aq);
      } else if(options.compareArgs("BASIS", "BERN")) {
        elliptic->partialIpdgKernel(mesh->NinternalElements,
            mesh->o_internalElementIds,
            mesh->o_vmapM,
            mesh->o_vmapP,
            lambda,
            elliptic->tau,
            mesh->o_vgeo,
            mesh->o_sgeo,
            elliptic->o_EToB,
            mesh->o_D1ids,
            mesh->o_D2ids,
            mesh->o_D3ids,
            mesh->o_Dvals,
            mesh->o_L0vals,
            mesh->o_ELids,
            mesh->o_ELvals,
            mesh->o_BBMM,
            elliptic->o_grad,
            o_Aq);
      }
    }

    if(elliptic->allNeumann) {
      o_tmp.copyTo(tmp);

      for(dlong n=0;n<Nblock;++n)
        alpha += tmp[n];

      MPI_Allreduce(&alpha, &alphaG, 1, MPI_DFLOAT, MPI_SUM, mesh->comm);
      alphaG *= elliptic->allNeumannPenalty*elliptic->allNeumannScale*elliptic->allNeumannScale;
    }

    ellipticEndHaloExchange(elliptic, o_q, mesh->Np, recvBuffer);

    if(mesh->totalHaloPairs){
      offset = mesh->Nelements;
      if(options.compareArgs("BASIS", "NODAL")) {
        elliptic->partialGradientKernel(mesh->totalHaloPairs,
            offset,
            mesh->o_vgeo,
            mesh->o_Dmatrices,
            o_q,
            elliptic->o_grad);
      } else if(options.compareArgs("BASIS", "BERN")) {
        elliptic->partialGradientKernel(mesh->totalHaloPairs,
            offset,
            mesh->o_vgeo,
            mesh->o_D1ids,
            mesh->o_D2ids,
            mesh->o_D3ids,
            mesh->o_Dvals,
            o_q,
            elliptic->o_grad);
      }
    }

    if(mesh->NnotInternalElements) {
      if(options.compareArgs("BASIS", "NODAL")) {
        elliptic->partialIpdgKernel(mesh->NnotInternalElements,
            mesh->o_notInternalElementIds,
            mesh->o_vmapM,
            mesh->o_vmapP,
            lambda,
            elliptic->tau,
            mesh->o_vgeo,
            mesh->o_sgeo,
            elliptic->o_EToB,
            mesh->o_Dmatrices,
            mesh->o_LIFTT,
            mesh->o_MM,
            elliptic->o_grad,
            o_Aq);
      } else if(options.compareArgs("BASIS", "BERN")) {
        elliptic->partialIpdgKernel(mesh->NnotInternalElements,
            mesh->o_notInternalElementIds,
            mesh->o_vmapM,
            mesh->o_vmapP,
            lambda,
            elliptic->tau,
            mesh->o_vgeo,
            mesh->o_sgeo,
            elliptic->o_EToB,
            mesh->o_D1ids,
            mesh->o_D2ids,
            mesh->o_D3ids,
            mesh->o_Dvals,
            mesh->o_L0vals,
            mesh->o_ELids,
            mesh->o_ELvals,
            mesh->o_BBMM,
            elliptic->o_grad,
            o_Aq);
      }
    }

    if(elliptic->allNeumann)
      mesh->addScalarKernel(mesh->Nelements*mesh->Np, alphaG, o_Aq);
  } 

  occaTimerToc(mesh->device,"AxKernel");
}
