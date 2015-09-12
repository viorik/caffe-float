#include <algorithm>
#include <limits>
#include <vector>

#include "caffe/common.hpp"
#include "caffe/layer.hpp"
#include "caffe/syncedmem.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {


template <typename Dtype>
__global__ void DropoutChannelForward(const int n, const Dtype* in,
    const Dtype* mask, const Dtype threshold, const float scale,
    Dtype* out) {
  CUDA_KERNEL_LOOP(index, n) {
    out[index] = in[index] * (mask[index] > threshold) * scale;
  }
}

template <typename Dtype>
void DropoutChannelLayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  const Dtype* bottom_data = bottom[0]->gpu_data();
  Dtype* top_data = top[0]->mutable_gpu_data();
  const int count = bottom[0]->count();
  if (Caffe::phase() == Caffe::TRAIN) {
    const Dtype* const_vec = rand_vec_.gpu_data();
    Dtype* vec = rand_vec_.mutable_gpu_data();
    Dtype* mask = rand_mat_.mutable_gpu_data();

    caffe_gpu_rng_uniform(rand_vec_.count(), Dtype(0), Dtype(1), vec);

    caffe_gpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, 
                          N_ * C_, H_ * W_, 1, Dtype(1),
                          const_vec, spatial_sum_multiplier_.gpu_data(),
                          Dtype(0), mask);

    // set thresholds
    // NOLINT_NEXT_LINE(whitespace/operators)
    DropoutChannelForward<Dtype><<<CAFFE_GET_BLOCKS(count), CAFFE_CUDA_NUM_THREADS>>>(
        count, bottom_data, mask, threshold_, scale_, top_data);
    CUDA_POST_KERNEL_CHECK;
  } else {
    caffe_copy(count, bottom_data, top_data);
  }
}

template <typename Dtype>
__global__ void DropoutChannelBackward(const int n, const Dtype* in_diff,
    const Dtype* mask, const Dtype threshold, const float scale,
    Dtype* out_diff) {
  CUDA_KERNEL_LOOP(index, n) {
    out_diff[index] = in_diff[index] * scale * (mask[index] > threshold);
  }
}

template <typename Dtype>
void DropoutChannelLayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  if (propagate_down[0]) {
    const Dtype* top_diff = top[0]->gpu_diff();
    Dtype* bottom_diff = bottom[0]->mutable_gpu_diff();
    if (Caffe::phase() == Caffe::TRAIN) {
      const Dtype* mask = rand_mat_.gpu_data();
      const int count = bottom[0]->count();
      // NOLINT_NEXT_LINE(whitespace/operators)
      DropoutChannelBackward<Dtype><<<CAFFE_GET_BLOCKS(count),
        CAFFE_CUDA_NUM_THREADS>>>(
          count, top_diff, mask, threshold_, scale_, bottom_diff);
      CUDA_POST_KERNEL_CHECK;
    } else {
      caffe_copy(top[0]->count(), top_diff, bottom_diff);
    }
  }
}

INSTANTIATE_LAYER_GPU_FUNCS(DropoutChannelLayer);


}  // namespace caffe
