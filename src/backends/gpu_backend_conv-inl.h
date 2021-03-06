#ifndef SRC_BACKENDS_GPU_BACKEND_CONV_INL_H_
#define SRC_BACKENDS_GPU_BACKEND_CONV_INL_H_

static void Convolution2DForwardFunc(
  const GPUTensor<DType>* input,
  const GPUTensor<DType>* filter,
  GPUTensor<DType>* output,
  ConvolutionContext<GPUTensor, DType>* context) {
  // shape decode
  size_t NIN, C, H, W;
  size_t KF, CF, R, S;
  size_t NOUT, K, P, Q;
  size_t pad_h, pad_w;
  size_t str_h, str_w;
  Blitz2DBuffer(input->shape(), &NIN, &C, &H, &W);
  Blitz2DFilter(filter->shape(), &KF, &CF, &R, &S);
  Blitz2DBuffer(output->shape(), &NOUT, &K, &P, &Q);
  context->CheckInputDataLayout(NIN, C, H, W);
  context->CheckFilterDataLayout(KF, CF, R, S);
  context->CheckOutputDataLayout(NOUT, K, P, Q);
  pad_h = context->pad_h();
  pad_w = context->pad_w();
  str_h = context->str_h();
  str_w = context->str_w();
  GPUTensor<DType>* workspace = context->workspace();
  // offset
  size_t nCHW = 0;
  size_t nKPQ = 0;
  // dims
  const size_t CHW = C * H * W;
  const size_t PQ = P * Q;
  const size_t KPQ = K * PQ;
  const size_t CRS = C * R * S;
  output->Fill(0);
  // time counter
  #ifdef BLITZ_PERFORMANCE
  cudaEvent_t start, stop;
  float elapsed_time = 0;
  BLITZ_GPU_TIMER_START(elapsed_time, start, stop);
  #endif
  switch (context->algorithm()) {
    case BLITZ_CONVOLUTION_SASS_DIRECT: {
      workspace->Fill(0);
      // transpose Input
      utils::GPUTrans(const_cast<DType*>(input->data()), 
        workspace->data(),
        NIN, CHW);
      // transpose Weight
      utils::GPUTrans(const_cast<DType*>(filter->data()), 
        workspace->Slice(input->size() + output->size()),
        K, CRS);
      // direct GEMM
      kernels::SassConvolution2DForward(
        workspace->data(),
        workspace->Slice(input->size()),
        workspace->Slice(input->size() + output->size()),
        NIN, C, H, W,
        R, S,
        K, P, Q,
        pad_h, pad_w,
        str_h, str_w);
      // transpose Output
      utils::GPUTrans(const_cast<DType*>(workspace->Slice(input->size())), 
        output->data(),
        KPQ, NIN);
      break;
    }
    case BLITZ_CONVOLUTION_BLAS_GEMM:
    case BLITZ_CONVOLUTION_SASS_GEMM: {
      for (size_t n = 0; n < NIN; ++n) {
        nCHW = n * CHW;
        nKPQ = n * KPQ;
        utils::Unpack2DDispatch<GPUTensor, DType>(input->Slice(nCHW),
          workspace->data(),
          C, H, W,
          R, S,
          P, Q,
          pad_h, pad_w,
          str_h, str_w,
          input->data_layout());
        if (context->algorithm() == BLITZ_CONVOLUTION_BLAS_GEMM) {
          utils::Gemm<GPUTensor, DType>(
            const_cast<GPUTensor<DType>*>(filter)->data(),
            workspace->data(),
            output->Slice(nKPQ),
            false, true,
            static_cast<DType>(1), static_cast<DType>(0),
            K, PQ, CRS);
        } else if (context->algorithm() == BLITZ_CONVOLUTION_SASS_GEMM) {
          kernels::SassGemm(const_cast<GPUTensor<DType>*>(filter)->data(),
            workspace->data(),
            output->Slice(nKPQ),
            false, true,
            static_cast<DType>(1), static_cast<DType>(0),
            K, PQ, CRS);
        }
      }
      break;
    }    
    default:
      LOG(FATAL) << "Unsupported algorithm type: " << context->algorithm();
      break;
  }
  #ifdef BLITZ_PERFORMANCE
  double computations = static_cast<double>(KPQ) * static_cast<double>(CRS) * static_cast<double>(2 * NIN);
  BLITZ_GPU_TIMER_END(elapsed_time, start, stop);
  BLITZ_GPU_TIMER_INFO(computations, elapsed_time);
  #endif  // BLITZ_PERFORMANCE
}

static void Convolution2DBackwardFunc(
  const GPUTensor<DType>* output,
  const GPUTensor<DType>* filter,
  GPUTensor<DType>* input,
  ConvolutionContext<GPUTensor, DType>* context) {
  // shape decode
  size_t NIN, C, H, W;
  size_t KF, CF, R, S;
  size_t NOUT, K, P, Q;
  size_t pad_h, pad_w;
  size_t str_h, str_w;
  Blitz2DBuffer(input->shape(), &NIN, &C, &H, &W);
  Blitz2DFilter(filter->shape(), &KF, &CF, &R, &S);
  Blitz2DBuffer(output->shape(), &NOUT, &K, &P, &Q);
  context->CheckInputDataLayout(NIN, C, H, W);
  context->CheckFilterDataLayout(KF, CF, R, S);
  context->CheckOutputDataLayout(NOUT, K, P, Q);
  pad_h = context->pad_h();
  pad_w = context->pad_w();
  str_h = context->str_h();
  str_w = context->str_w();
  GPUTensor<DType>* workspace = context->workspace();
  // offset
  size_t nCHW = 0;
  size_t nKPQ = 0;
  // dims
  const size_t CHW = C * H * W;
  const size_t PQ = P * Q;
  const size_t KPQ = K * PQ;
  const size_t CRS = C * R * S;
  input->Fill(0);
  // time counter
  #ifdef BLITZ_PERFORMANCE
  cudaEvent_t start, stop;
  float elapsed_time = 0;
  BLITZ_GPU_TIMER_START(elapsed_time, start, stop);
  #endif  // BLITZ_PERFORMANCE
  // init
  switch (context->algorithm()) {
    case BLITZ_CONVOLUTION_SASS_DIRECT: {
      workspace->Fill(0);
      // transpose output
      utils::GPUTrans(const_cast<DType*>(output->data()), 
        workspace->Slice(input->size()),
        NIN, KPQ);
      if (C % 64 != 0) {
        // direct GEMM
        kernels::SassConvolution2DBackward(
          workspace->data(),
          const_cast<DType*>(workspace->Slice(input->size())),
          const_cast<DType*>(filter->data()),
          NIN, C, H, W,
          R, S,
          K, P, Q,
          pad_h, pad_w,
          str_h, str_w);
      } else {
        // shuffle filter
        kernels::Filter2DShuffle(const_cast<DType*>(filter->data()), 
          workspace->Slice(input->size() + output->size()),
          K, C, R, S);
        // direct GEMM
        kernels::SassConvolution2DBackward(
          workspace->data(),
          const_cast<DType*>(workspace->Slice(input->size())),
          const_cast<DType*>(workspace->Slice(input->size() + output->size())),
          NIN, C, H, W,
          R, S,
          K, P, Q,
          pad_h, pad_w,
          str_h, str_w);
      }
      // transpose input
      utils::GPUTrans(const_cast<DType*>(workspace->data()), 
        input->data(), 
        CHW, NIN);
      break;
    }
    case BLITZ_CONVOLUTION_SASS_GEMM:
    case BLITZ_CONVOLUTION_BLAS_GEMM: {
      for (size_t n = 0; n < NIN; ++n) {
        nCHW = n * CHW;
        nKPQ = n * KPQ;
        if (context->algorithm() == BLITZ_CONVOLUTION_BLAS_GEMM) {
          utils::Gemm<GPUTensor, DType>(
            const_cast<GPUTensor<DType>*>(output)->Slice(nKPQ),
            const_cast<GPUTensor<DType>*>(filter)->data(),
            workspace->data(),
            true, false,
            static_cast<DType>(1), static_cast<DType>(0),
            PQ, CRS, K);
        } else if (context->algorithm() == BLITZ_CONVOLUTION_SASS_GEMM) {
          kernels::SassGemm(const_cast<GPUTensor<DType>*>(output)->Slice(nKPQ),
            const_cast<GPUTensor<DType>*>(filter)->data(),
            workspace->data(),
            true, false,
            static_cast<DType>(1), static_cast<DType>(0),
            PQ, CRS, K);
        }
        utils::Pack2DDispatch<GPUTensor, DType>(workspace->data(),
          input->Slice(nCHW),
          C, H, W,
          R, S,
          P, Q,
          pad_h, pad_w,
          str_h, str_w,
          input->data_layout());
      }
      break;
    }
    default:
      LOG(FATAL) << "Unsupported algorithm type: " << context->algorithm();
      break;
  }
  #ifdef BLITZ_PERFORMANCE
  double computations = static_cast<double>(KPQ) * static_cast<double>(CRS) * static_cast<double>(2 * NIN);
  BLITZ_GPU_TIMER_END(elapsed_time, start, stop);
  BLITZ_GPU_TIMER_INFO(computations, elapsed_time);
  #endif  // BLITZ_PERFORMANCE
}

static void Convolution2DUpdateFunc(
  const GPUTensor<DType>* input,
  const GPUTensor<DType>* output,
  GPUTensor<DType>* update,
  ConvolutionContext<GPUTensor, DType>* context) {
  // shape decode
  size_t NIN, C, H, W;
  size_t KF, CF, R, S;
  size_t NOUT, K, P, Q;
  size_t pad_h, pad_w;
  size_t str_h, str_w;
  Blitz2DBuffer(input->shape(), &NIN, &C, &H, &W);
  Blitz2DFilter(update->shape(), &KF, &CF, &R, &S);
  Blitz2DBuffer(output->shape(), &NOUT, &K, &P, &Q);
  context->CheckInputDataLayout(NIN, C, H, W);
  context->CheckFilterDataLayout(KF, CF, R, S);
  context->CheckOutputDataLayout(NOUT, K, P, Q);
  pad_h = context->pad_h();
  pad_w = context->pad_w();
  str_h = context->str_h();
  str_w = context->str_w();
  GPUTensor<DType>* workspace = context->workspace();
  // offset
  size_t nCHW = 0;
  size_t nKPQ = 0;
  // dims
  const size_t CHW = C * H * W;
  const size_t PQ = P * Q;
  const size_t KPQ = K * PQ;
  const size_t CRS = C * R * S;
  update->Fill(0);
  // time counter
  #ifdef BLITZ_PERFORMANCE
  cudaEvent_t start, stop;
  float elapsed_time = 0;
  BLITZ_GPU_TIMER_START(elapsed_time, start, stop);
  #endif  // BLITZ_PERFORMANCE
  switch (context->algorithm()) {
    case BLITZ_CONVOLUTION_SASS_DIRECT: {
      workspace->Fill(0);
      // transpose input
      utils::GPUTrans(const_cast<DType*>(input->data()), 
        workspace->data(), 
        NIN, CHW);
      // transpose output
      utils::GPUTrans(const_cast<DType*>(output->data()), 
        workspace->Slice(input->size()), 
        NIN, KPQ);
      kernels::SassConvolution2DUpdate(
        const_cast<DType*>(workspace->data()),
        const_cast<DType*>(workspace->Slice(input->size())),
        workspace->Slice(input->size() + output->size()),
        NIN, C, H, W,
        R, S,
        K, P, Q,
        pad_h, pad_w,
        str_h, str_w);
      // transpose update
      utils::GPUTrans(
        const_cast<DType*>(workspace->Slice(input->size() + output->size())),
        update->data(),
        CRS, K);
      break;
    }
    case BLITZ_CONVOLUTION_SASS_GEMM:
    case BLITZ_CONVOLUTION_BLAS_GEMM: {
      for (size_t n = 0; n < NIN; ++n) {
        nCHW = n * CHW;
        nKPQ = n * KPQ;
        utils::Unpack2DDispatch<GPUTensor, DType>(input->Slice(nCHW),
          workspace->data(),
          C, H, W,
          R, S,
          P, Q,
          pad_h, pad_w,
          str_h, str_w,
          input->data_layout());
        if (context->algorithm() == BLITZ_CONVOLUTION_BLAS_GEMM) {
          utils::Gemm<GPUTensor, DType>(
            const_cast<GPUTensor<DType>*>(output)->Slice(nKPQ),
            const_cast<GPUTensor<DType>*>(workspace)->data(),
            update->data(),
            false, false,
            static_cast<DType>(1), static_cast<DType>(1),
            K, CRS, PQ);
        } else if (context->algorithm() == BLITZ_CONVOLUTION_SASS_GEMM) {
          kernels::SassGemm(
            const_cast<GPUTensor<DType>*>(output)->Slice(nKPQ),
            const_cast<GPUTensor<DType>*>(workspace)->data(),
            update->data(),
            false, false,
            static_cast<DType>(1), static_cast<DType>(1),
            K, CRS, PQ);
        }
      }
      break;
    }
    default:
      LOG(FATAL) << "Unsupported algorithm type: " << context->algorithm();
      break;
  }
  #ifdef BLITZ_PERFORMANCE
  double computations = static_cast<double>(KPQ) * static_cast<double>(CRS) * static_cast<double>(2 * NIN);
  BLITZ_GPU_TIMER_END(elapsed_time, start, stop);
  BLITZ_GPU_TIMER_INFO(computations, elapsed_time);
  #endif  // BLITZ_PERFORMANCE
}

#endif  // SRC_BACKENDS_GPU_BACKEND_CONV_INL_H_
