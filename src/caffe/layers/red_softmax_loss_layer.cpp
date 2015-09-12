#include <algorithm>
#include <cfloat>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

using std::min;
using std::max;

template <typename Dtype>
void RedSoftmaxWithLossLayer<Dtype>::LayerSetUp(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  LossLayer<Dtype>::LayerSetUp(bottom, top);
  softmax_bottom_vec_.clear();
  softmax_bottom_vec_.push_back(bottom[0]);
  softmax_top_vec_.clear();
  softmax_top_vec_.push_back(&prob_);
  softmax_layer_->SetUp(softmax_bottom_vec_, softmax_top_vec_);

  has_ignore_label_ =
    this->layer_param_.loss_param().has_ignore_label();
  if (has_ignore_label_) {
    ignore_label_ = this->layer_param_.loss_param().ignore_label();
  }
  normalize_ = this->layer_param_.loss_param().normalize();

  // set redendant class num & class num
  red_cls_num_ = bottom[0]->channels();
  cls_num_ = red_cls_num_;
  if (this->layer_param_.red_softmax_loss_param().has_class_num()) {
    cls_num_ = this->layer_param_.red_softmax_loss_param().class_num();
  }
}

template <typename Dtype>
void RedSoftmaxWithLossLayer<Dtype>::Reshape(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  LossLayer<Dtype>::Reshape(bottom, top);
  softmax_layer_->Reshape(softmax_bottom_vec_, softmax_top_vec_);
  target_sum_.ReshapeLike(*bottom[1]);
  if (top.size() >= 2) {
    // softmax output
    top[1]->ReshapeLike(*bottom[0]);
  }
}

template <typename Dtype>
void RedSoftmaxWithLossLayer<Dtype>::Forward_cpu(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  // The forward pass computes the softmax prob values.
  softmax_layer_->Forward(softmax_bottom_vec_, softmax_top_vec_);
  const Dtype* prob_data = prob_.cpu_data();
  const Dtype* label = bottom[1]->cpu_data();
  Dtype* target_sum_data = target_sum_.mutable_cpu_data();
  int num = prob_.num();
  int dim = prob_.count() / num;
  int spatial_dim = prob_.height() * prob_.width();
  int count = 0;
  Dtype loss = 0;
  for (int i = 0; i < num; ++i) {
    for (int j = 0; j < spatial_dim; j++) {
      const int label_value = static_cast<int>(label[i * spatial_dim + j]);
      if (has_ignore_label_ && label_value == ignore_label_) {
        continue;
      }
      DCHECK_GE(label_value, 0);
      DCHECK_LT(label_value, prob_.channels());

      const int target_from = label_value * red_cls_num_ / cls_num_;
      const int target_to = min(red_cls_num_, (label_value+1) * red_cls_num_ / cls_num_);
      Dtype target_sum_val = 0;
      for (int c = target_from; c < target_to; c++) {
        target_sum_val += prob_data[i * dim + c * spatial_dim + j];
      }
      // compute loss
      loss -= log(std::max(target_sum_val, Dtype(FLT_MIN)));

      // save target sum data to use it for backpropagation
      target_sum_data[i * spatial_dim + j] = target_sum_val;

      ++count;
    }
  }
  if (normalize_) {
    top[0]->mutable_cpu_data()[0] = loss / count;
  } else {
    top[0]->mutable_cpu_data()[0] = loss / num;
  }
  if (top.size() == 2) {
    top[1]->ShareData(prob_);
  }
}

template <typename Dtype>
void RedSoftmaxWithLossLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  if (propagate_down[1]) {
    LOG(FATAL) << this->type_name()
               << " Layer cannot backpropagate to label inputs.";
  }
  if (propagate_down[0]) {
    Dtype* bottom_diff = bottom[0]->mutable_cpu_diff();
    const Dtype* prob_data = prob_.cpu_data();
    caffe_copy(prob_.count(), prob_data, bottom_diff);
    const Dtype* label = bottom[1]->cpu_data();
    const Dtype* target_sum_data = target_sum_.cpu_data();
    int num = prob_.num();
    int dim = prob_.count() / num;
    int spatial_dim = prob_.height() * prob_.width();
    int count = 0;
    for (int i = 0; i < num; ++i) {
      for (int j = 0; j < spatial_dim; ++j) {
        const int label_value = static_cast<int>(label[i * spatial_dim + j]);
        if (has_ignore_label_ && label_value == ignore_label_) {
          for (int c = 0; c < bottom[0]->channels(); ++c) {
            bottom_diff[i * dim + c * spatial_dim + j] = 0;
          }
        } else {
      	  const int target_from = label_value * red_cls_num_ / cls_num_;
          const int target_to = min(red_cls_num_, (label_value+1) * red_cls_num_ / cls_num_);
          const Dtype target_sum_val = target_sum_data[i * spatial_dim + j];
          for (int c = target_from; c < target_to; c++) {
            Dtype prob_val = prob_data[i * dim + c * spatial_dim + j];
            bottom_diff[i * dim + c * spatial_dim + j] -= prob_val / target_sum_val; 
          }   
          ++count;
        }
      }
    }
    // Scale gradient
    const Dtype loss_weight = top[0]->cpu_diff()[0];
    if (normalize_) {
      caffe_scal(prob_.count(), loss_weight / count, bottom_diff);
    } else {
      caffe_scal(prob_.count(), loss_weight / num, bottom_diff);
    }
  }
}

INSTANTIATE_CLASS(RedSoftmaxWithLossLayer);
REGISTER_LAYER_CLASS(RED_SOFTMAX_LOSS, RedSoftmaxWithLossLayer);

}  // namespace caffe
