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

#include "acoustics3D.h"

void acousticsSetup3D(mesh3D *mesh){

  mesh->Nfields = 4;
  
  // compute samples of q at interpolation nodes
  mesh->q    = (dfloat*) calloc((mesh->totalHaloPairs+mesh->Nelements)*mesh->Np*mesh->Nfields, sizeof(dfloat));
  mesh->rhsq = (dfloat*) calloc(mesh->Nelements*mesh->Np*mesh->Nfields,
				sizeof(dfloat));
  mesh->resq = (dfloat*) calloc(mesh->Nelements*mesh->Np*mesh->Nfields,
				sizeof(dfloat));

  // fix this later (initial conditions)
  int cnt = 0;
  dfloat time = 0;
  for(int e=0;e<mesh->Nelements;++e){
    for(int n=0;n<mesh->Np;++n){
      dfloat x = mesh->x[n + mesh->Np*e];
      dfloat y = mesh->y[n + mesh->Np*e];
      dfloat z = mesh->z[n + mesh->Np*e];
      
      acousticsCavitySolution3D(x, y, z, time,
				mesh->q+cnt,
				mesh->q+cnt+1,
				mesh->q+cnt+2,
				mesh->q+cnt+3);

      cnt += mesh->Nfields;
    }
    printf("\n");
  }

  
  mesh->c2 = (dfloat*) calloc(mesh->Nelements*mesh->cubNp,sizeof(dfloat));  
  for(int e=0;e<mesh->Nelements;++e){ 
    
    int id = e*mesh->Nverts;
    
    dfloat xe1 = mesh->EX[id+0]; /* x-coordinates of vertices */
    dfloat xe2 = mesh->EX[id+1];
    dfloat xe3 = mesh->EX[id+2];
    dfloat xe4 = mesh->EX[id+3];
    
    dfloat ye1 = mesh->EY[id+0]; /* y-coordinates of vertices */
    dfloat ye2 = mesh->EY[id+1];
    dfloat ye3 = mesh->EY[id+2];
    dfloat ye4 = mesh->EY[id+3];
    
    dfloat ze1 = mesh->EZ[id+0]; /* z-coordinates of vertices */
    dfloat ze2 = mesh->EZ[id+1];
    dfloat ze3 = mesh->EZ[id+2];
    dfloat ze4 = mesh->EZ[id+3];
    
    for(int n=0;n<mesh->cubNp;++n){ /* for each node */
      
      /* (r,s,t) coordinates of cubature nodes*/
      dfloat rn = mesh->cubr[n]; 
      dfloat sn = mesh->cubs[n];
      dfloat tn = mesh->cubt[n];

      /* physical coordinate of interpolation node */
      dfloat cubx = -0.5*(1+rn+sn+tn)*xe1 + 0.5*(1+rn)*xe2 + 0.5*(1+sn)*xe3 + 0.5*(1+tn)*xe4;
      dfloat cuby = -0.5*(1+rn+sn+tn)*ye1 + 0.5*(1+rn)*ye2 + 0.5*(1+sn)*ye3 + 0.5*(1+tn)*ye4;
      dfloat cubz = -0.5*(1+rn+sn+tn)*ze1 + 0.5*(1+rn)*ze2 + 0.5*(1+sn)*ze3 + 0.5*(1+tn)*ze4;

      mesh->c2[n+e*mesh->cubNp] = 1.0;
      //mesh->c2[n+e*mesh->cubNp] = 1.0 + .5*sin(M_PI*cubx)*sin(M_PI*cuby)*sin(M_PI*cubz);
      
    }
  }

  dfloat *invMc;

#define storeMatrices 1
#if storeMatrices
  invMc = (dfloat*) calloc(mesh->Np*mesh->Np*mesh->Nelements,sizeof(dfloat));
  
  for(int e=0;e<mesh->Nelements;++e){
    for (int j = 0; j < mesh->Np; ++j){
      for (int i = 0; i < mesh->Np; ++i){
	// store arbitrary values for testing
	invMc[i + j*mesh->Np + e*mesh->Np*mesh->Np] =
	  (dfloat) i+j+e;
      }
    }
  }
  
#else
  // just store a dummy array
  invMc = (dfloat*) calloc(1, sizeof(dfloat));  
#endif

  // set penalty parameter
  mesh->Lambda2 = 0.5;
  
  // set time step
  dfloat hmin = 1e9;
  for(int e=0;e<mesh->Nelements;++e){  

    for(int f=0;f<mesh->Nfaces;++f){
      int sid = mesh->Nsgeo*(mesh->Nfaces*e + f);
      dfloat sJ   = mesh->sgeo[sid + SJID];
      dfloat invJ = mesh->sgeo[sid + IJID];

      // sJ = L/2, J = A/2,   sJ/J = L/A = L/(0.5*h*L) = 2/h
      // h = 0.5/(sJ/J)
      
      dfloat hest = .5/(sJ*invJ);

      hmin = mymin(hmin, hest);
    }
  }
  dfloat cfl = 1; // depends on the stability region size

  dfloat dt = cfl*hmin/((mesh->N+1)*(mesh->N+1)*mesh->Lambda2);

  // MPI_Allreduce to get global minimum dt
  MPI_Allreduce(&dt, &(mesh->dt), 1, MPI_DFLOAT, MPI_MIN, MPI_COMM_WORLD);

  //
  mesh->finalTime = .4;
  mesh->NtimeSteps = mesh->finalTime/mesh->dt;
  mesh->dt = mesh->finalTime/mesh->NtimeSteps;

  // errorStep
  mesh->errorStep = 25;

  printf("dt = %g\n", mesh->dt);

  // output mesh
  meshVTU3D(mesh, "foo.vtu");

  // OCCA build stuff
  char deviceConfig[BUFSIZ];
  int rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  // use rank to choose DEVICE
  sprintf(deviceConfig, "mode = CUDA, deviceID = %d", 0);
  //  sprintf(deviceConfig, "mode = OpenCL, deviceID = 0, platformID = 1");
  //  sprintf(deviceConfig, "mode = OpenMP, deviceID = %d", 1);

  mesh->device.setup(deviceConfig);

  // build Dr, Ds, LIFT transposes
  dfloat *DrT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  dfloat *DsT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  dfloat *DtT = (dfloat*) calloc(mesh->Np*mesh->Np, sizeof(dfloat));
  for(int n=0;n<mesh->Np;++n){
    for(int m=0;m<mesh->Np;++m){
      DrT[n+m*mesh->Np] = mesh->Dr[n*mesh->Np+m];
      DsT[n+m*mesh->Np] = mesh->Ds[n*mesh->Np+m];
      DtT[n+m*mesh->Np] = mesh->Dt[n*mesh->Np+m];
    }
  }

  // build cubature matrix transposes
  dfloat *cubProjectT = (dfloat*) calloc(mesh->cubNp*mesh->Np, sizeof(dfloat));
  dfloat *cubInterpT = (dfloat*) calloc(mesh->cubNp*mesh->Np, sizeof(dfloat));
  for(int n=0;n<mesh->Np;++n){
    for(int m=0;m<mesh->cubNp;++m){
      cubInterpT[m+n*mesh->cubNp] = mesh->cubInterp[m*mesh->Np+n];
      cubProjectT[n+m*mesh->Np] = mesh->cubProject[n*mesh->cubNp+m];
    }
  }

  dfloat *LIFTT = (dfloat*) calloc(mesh->Np*mesh->Nfaces*mesh->Nfp, sizeof(dfloat));
  for(int n=0;n<mesh->Np;++n){
    for(int m=0;m<mesh->Nfaces*mesh->Nfp;++m){
      LIFTT[n+m*mesh->Np] = mesh->LIFT[n*mesh->Nfp*mesh->Nfaces+m];
    }
  }

  // OCCA allocate device memory (remember to go back for halo)
  mesh->o_q =
    mesh->device.malloc(mesh->Np*(mesh->totalHaloPairs+mesh->Nelements)*mesh->Nfields*sizeof(dfloat), mesh->q);
  mesh->o_rhsq =
    mesh->device.malloc(mesh->Np*mesh->Nelements*mesh->Nfields*sizeof(dfloat), mesh->rhsq);
  mesh->o_resq =
    mesh->device.malloc(mesh->Np*mesh->Nelements*mesh->Nfields*sizeof(dfloat), mesh->resq);

  mesh->o_Dr = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), mesh->Dr);
  mesh->o_Ds = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), mesh->Ds);
  mesh->o_Dt = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), mesh->Dt);

  mesh->o_DrT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), DrT);
  mesh->o_DsT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), DsT);
  mesh->o_DtT = mesh->device.malloc(mesh->Np*mesh->Np*sizeof(dfloat), DtT);

  mesh->o_LIFT =
    mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
			mesh->LIFT);

  mesh->o_LIFTT =
    mesh->device.malloc(mesh->Np*mesh->Nfaces*mesh->Nfp*sizeof(dfloat),
			LIFTT);
  
  mesh->o_vgeo =
    mesh->device.malloc(mesh->Nelements*mesh->Nvgeo*sizeof(dfloat),
			mesh->vgeo);
  
  mesh->o_sgeo =
    mesh->device.malloc(mesh->Nelements*mesh->Nfaces*mesh->Nsgeo*sizeof(dfloat),
			mesh->sgeo);

  mesh->o_vmapM =
    mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(int),
			mesh->vmapM);

  mesh->o_vmapP =
    mesh->device.malloc(mesh->Nelements*mesh->Nfp*mesh->Nfaces*sizeof(int),
			mesh->vmapP);

  mesh->o_EToB =
    mesh->device.malloc(mesh->Nelements*mesh->Nfaces*sizeof(int),
			mesh->EToB);

  mesh->o_x =
    mesh->device.malloc(mesh->Nelements*mesh->Np*sizeof(dfloat), mesh->x);

  mesh->o_y =
    mesh->device.malloc(mesh->Nelements*mesh->Np*sizeof(dfloat), mesh->y);

  mesh->o_z =
    mesh->device.malloc(mesh->Nelements*mesh->Np*sizeof(dfloat), mesh->z);

  // for wadg
#if storeMatrices
  mesh->o_invMc =
    mesh->device.malloc(mesh->Np*mesh->Np*mesh->Nelements*sizeof(dfloat),
			invMc);
#endif
  mesh->o_cubInterpT =
    mesh->device.malloc(mesh->Np*mesh->cubNp*sizeof(dfloat),
			cubInterpT);  
  mesh->o_cubProjectT =
    mesh->device.malloc(mesh->Np*mesh->cubNp*sizeof(dfloat),
			cubProjectT);  
  mesh->o_c2 =
    mesh->device.malloc(mesh->cubNp*mesh->Nelements*sizeof(dfloat),
			mesh->c2);
  
  
  if(mesh->totalHaloPairs>0){
    // copy halo element list to DEVICE
    mesh->o_haloElementList =
      mesh->device.malloc(mesh->totalHaloPairs*sizeof(int), mesh->haloElementList);
    
    // temporary DEVICE buffer for halo (maximum size Nfields*Np for dfloat)
    mesh->o_haloBuffer =
      mesh->device.malloc(mesh->totalHaloPairs*mesh->Np*mesh->Nfields*sizeof(dfloat));
  }
  
  occa::kernelInfo kernelInfo;

  kernelInfo.addDefine("p_Nfields", mesh->Nfields);
  kernelInfo.addDefine("p_N", mesh->N);
  kernelInfo.addDefine("p_Np", mesh->Np);
  kernelInfo.addDefine("p_cubNp", mesh->cubNp);  
  kernelInfo.addDefine("p_Nfp", mesh->Nfp);
  kernelInfo.addDefine("p_Nfaces", mesh->Nfaces);
  kernelInfo.addDefine("p_NfacesNfp", mesh->Nfp*mesh->Nfaces);
  kernelInfo.addDefine("p_Nvgeo", mesh->Nvgeo);
  kernelInfo.addDefine("p_Nsgeo", mesh->Nsgeo);

  int maxNodes = mymax(mesh->Np, (mesh->Nfp*mesh->Nfaces));
  kernelInfo.addDefine("p_maxNodes", maxNodes);

  int NblockV = 512/mesh->Np; // works for CUDA
  kernelInfo.addDefine("p_NblockV", NblockV);

  int NblockS = 512/maxNodes; // works for CUDA
  kernelInfo.addDefine("p_NblockS", NblockS);
  
  kernelInfo.addDefine("p_Lambda2", 0.5f);

  if(sizeof(dfloat)==4){
    kernelInfo.addDefine("dfloat","float");
    kernelInfo.addDefine("dfloat4","float4");
  }
  if(sizeof(dfloat)==8){
    kernelInfo.addDefine("dfloat","double");
    kernelInfo.addDefine("dfloat4","double4");
  }

  if(sizeof(int)==4){
    kernelInfo.addDefine("int","int");
  }
  if(sizeof(int)==8){
    kernelInfo.addDefine("int","long long int");
  }

  if(mesh->device.mode()=="CUDA"){ // add backend compiler optimization for CUDA
    kernelInfo.addCompilerFlag("--ftz=true");
    kernelInfo.addCompilerFlag("--prec-div=false");
    kernelInfo.addCompilerFlag("--prec-sqrt=false");
    kernelInfo.addCompilerFlag("--use_fast_math");
    kernelInfo.addCompilerFlag("--fmad=true"); // compiler option for cuda
  }

  mesh->volumeKernel =
    mesh->device.buildKernelFromSource("okl/acousticsVolume3D.okl",
				       "acousticsVolume3D_o0",
				       kernelInfo);

  mesh->surfaceKernel =
    mesh->device.buildKernelFromSource("okl/acousticsSurface3D.okl",
				       "acousticsSurface3D_s0",
				       kernelInfo);

#if 0
  mesh->updateKernel =
    mesh->device.buildKernelFromSource("okl/acousticsUpdate3D.okl",
				       "acousticsUpdate3D",
				       kernelInfo);
#else
  mesh->updateKernel =
    mesh->device.buildKernelFromSource("okl/wadgUpdateKernels.okl",
				       "acousticsUpdate3D_wadg",
				       kernelInfo);  
#endif
  
  mesh->haloExtractKernel =
    mesh->device.buildKernelFromSource("okl/meshHaloExtract3D.okl",
				       "meshHaloExtract3D",
				       kernelInfo);

  int Ntests = 10;

#define maxNkernels 100

  int NvolumeKernels = 7;
  occa::kernel *acousticsVolumeKernels = new occa::kernel[maxNkernels];
  char kernelNames[maxNkernels][BUFSIZ];
  double bestElapsed = 1e9;
  
  for(int ker=0;ker<NvolumeKernels;++ker){
    sprintf(kernelNames[ker], "acousticsVolume3D_o%d", ker);
    
    acousticsVolumeKernels[ker] =
      mesh->device.buildKernelFromSource("okl/acousticsVolume3D.okl", kernelNames[ker], kernelInfo);
    
    mesh->device.finish();
    occa::tic(kernelNames[ker]);
    for(int test=0;test<Ntests;++test)
      acousticsVolumeKernels[ker](mesh->Nelements,
				      mesh->o_vgeo,
				      mesh->o_DrT,
				      mesh->o_DsT,
				      mesh->o_DtT,
				      mesh->o_q,
				      mesh->o_rhsq);
    mesh->device.finish();
    double elapsed = occa::toc(kernelNames[ker]);
    if(elapsed<bestElapsed){
      mesh->volumeKernel = acousticsVolumeKernels[ker];
      printf("promoting kernel: %d (time %g)\n", ker, elapsed);
      bestElapsed = elapsed;
    }
    else{
      printf("not promoting kernel: %d (time %g)\n", ker, elapsed);
    }
  }

  int NsurfaceKernels = 5;
  char surfaceKernelNames[maxNkernels][BUFSIZ];
  occa::kernel *acousticsSurfaceKernels = new occa::kernel[maxNkernels];
  bestElapsed = 1e9;
  
  for(int ker=0;ker<NsurfaceKernels;++ker){
    sprintf(surfaceKernelNames[ker], "acousticsSurface3D_s%d", ker);
    
    acousticsSurfaceKernels[ker] =
      mesh->device.buildKernelFromSource("okl/acousticsSurface3D.okl",
					 surfaceKernelNames[ker], kernelInfo);
    
    mesh->device.finish();
    occa::tic(surfaceKernelNames[ker]);
    for(int test=0;test<Ntests;++test){
      dfloat t  = 0;
      acousticsSurfaceKernels[ker](mesh->Nelements,
				       mesh->o_sgeo,
				       mesh->o_LIFTT,
				       mesh->o_vmapM,
				       mesh->o_vmapP,
				       mesh->o_EToB,
				       t,
				       mesh->o_x,
				       mesh->o_y,
				       mesh->o_z,
				       mesh->o_q,
				       mesh->o_rhsq);
    }
    mesh->device.finish();
    double elapsed = occa::toc(surfaceKernelNames[ker]);
    if(elapsed<bestElapsed){
      mesh->surfaceKernel = acousticsSurfaceKernels[ker];
      printf("promoting kernel: %d (time %g)\n", ker, elapsed);
      bestElapsed = elapsed;
    }
    else{
      printf("not promoting kernel: %d (time %g)\n", ker, elapsed);
    }
  }


}
