#include "elliptic.h"

void ellipticPreconditioner(elliptic_t *elliptic, dfloat lambda,
                            occa::memory &o_r, occa::memory &o_z){

  mesh_t *mesh = elliptic->mesh;
  precon_t *precon = elliptic->precon;
  setupAide options = elliptic->options;
  
  if (   options.compareArgs("PRECONDITIONER", "FULLALMOND")
      || options.compareArgs("PRECONDITIONER", "MULTIGRID")) {

    occaTimerTic(mesh->device,"parALMOND");
    parAlmondPrecon(precon->parAlmond, o_z, o_r);
    occaTimerToc(mesh->device,"parALMOND");

  } else if(options.compareArgs("PRECONDITIONER", "MASSMATRIX")){

    dfloat invLambda = 1./lambda;

    if (options.compareArgs("DISCRETIZATION", "IPDG")) {
      occaTimerTic(mesh->device,"blockJacobiKernel");
      precon->blockJacobiKernel(mesh->Nelements, invLambda, mesh->o_vgeo, precon->o_invMM, o_r, o_z);
      occaTimerToc(mesh->device,"blockJacobiKernel");
    } else if (options.compareArgs("DISCRETIZATION", "CONTINUOUS")) {
      ogs_t *ogs = elliptic->mesh->ogs;
      int one = 1;
      dlong dOne = 1;

      elliptic->dotMultiplyKernel(mesh->Nelements*mesh->Np, ogs->o_invDegree, o_r, elliptic->o_rtmp);

      //pre-mask
      //if (elliptic->Nmasked) mesh->maskKernel(elliptic->Nmasked, elliptic->o_maskIds, o_r);

      if(ogs->NhaloGather) {
        precon->partialblockJacobiKernel(elliptic->NglobalGatherElements, 
                                elliptic->o_globalGatherElementList,
                                invLambda, mesh->o_vgeo, precon->o_invMM, elliptic->o_rtmp, o_z);
        mesh->device.finish();
        mesh->device.setStream(elliptic->dataStream);
        mesh->gatherKernel(ogs->NhaloGather, ogs->o_haloGatherOffsets, ogs->o_haloGatherLocalIds, one, dOne, o_z, ogs->o_haloGatherTmp);
        ogs->o_haloGatherTmp.asyncCopyTo(ogs->haloGatherTmp);
        mesh->device.setStream(elliptic->defaultStream);
      }
      if(elliptic->NlocalGatherElements){
        precon->partialblockJacobiKernel(elliptic->NlocalGatherElements, 
                                elliptic->o_localGatherElementList,
                                invLambda, mesh->o_vgeo, precon->o_invMM, elliptic->o_rtmp, o_z);
      }

      // finalize gather using local and global contributions
      if(ogs->NnonHaloGather) mesh->gatherScatterKernel(ogs->NnonHaloGather, ogs->o_nonHaloGatherOffsets, ogs->o_nonHaloGatherLocalIds, one, dOne, o_z);

      // C0 halo gather-scatter (on data stream)
      if(ogs->NhaloGather) {
        mesh->device.setStream(elliptic->dataStream);
        mesh->device.finish();

        // MPI based gather scatter using libgs
        gsParallelGatherScatter(ogs->haloGsh, ogs->haloGatherTmp, dfloatString, "add");

        // copy totally gather halo data back from HOST to DEVICE
        ogs->o_haloGatherTmp.asyncCopyFrom(ogs->haloGatherTmp);

        // do scatter back to local nodes
        mesh->scatterKernel(ogs->NhaloGather, ogs->o_haloGatherOffsets, ogs->o_haloGatherLocalIds, one, dOne, ogs->o_haloGatherTmp, o_z);
        mesh->device.setStream(elliptic->defaultStream);
      }

      mesh->device.finish();    
      mesh->device.setStream(elliptic->dataStream);
      mesh->device.finish();    
      mesh->device.setStream(elliptic->defaultStream);

      elliptic->dotMultiplyKernel(mesh->Nelements*mesh->Np, ogs->o_invDegree, o_z, o_z);

      //post-mask
      if (elliptic->Nmasked) mesh->maskKernel(elliptic->Nmasked, elliptic->o_maskIds, o_z);
    }

  } else if (options.compareArgs("PRECONDITIONER", "SEMFEM")) {

    if (elliptic->elementType==TRIANGLES||elliptic->elementType==TETRAHEDRA) {
      o_z.copyFrom(o_r);
      elliptic->dotMultiplyKernel(mesh->Nelements*mesh->Np, elliptic->o_invDegree, o_z, o_z);
      precon->SEMFEMInterpKernel(mesh->Nelements,mesh->o_SEMFEMAnterp,o_z,precon->o_rFEM);
      meshParallelGather(mesh, precon->FEMogs, precon->o_rFEM, precon->o_GrFEM);
      occaTimerTic(mesh->device,"parALMOND");
      parAlmondPrecon(precon->parAlmond, precon->o_GzFEM, precon->o_GrFEM);
      occaTimerToc(mesh->device,"parALMOND");
      meshParallelScatter(mesh, precon->FEMogs, precon->o_GzFEM, precon->o_zFEM);
      precon->SEMFEMAnterpKernel(mesh->Nelements,mesh->o_SEMFEMAnterp,precon->o_zFEM,o_z);
      elliptic->dotMultiplyKernel(mesh->Nelements*mesh->Np, elliptic->o_invDegree, o_z, o_z);

      ellipticParallelGatherScatter(mesh, mesh->ogs, o_z, dfloatString, "add");
      if (elliptic->Nmasked) mesh->maskKernel(elliptic->Nmasked, elliptic->o_maskIds, o_z);
    } else {
      occaTimerTic(mesh->device,"parALMOND");
      parAlmondPrecon(precon->parAlmond, o_z, o_r);
      occaTimerToc(mesh->device,"parALMOND");
    }

  } else if(options.compareArgs("PRECONDITIONER", "JACOBI")){

    dlong Ntotal = mesh->Np*mesh->Nelements;
    // Jacobi preconditioner
    occaTimerTic(mesh->device,"dotDivideKernel");
    elliptic->dotMultiplyKernel(Ntotal, o_r, precon->o_invDiagA, o_z);
    occaTimerToc(mesh->device,"dotDivideKernel");
  
  } else{ // turn off preconditioner
    o_z.copyFrom(o_r);
  }
}
