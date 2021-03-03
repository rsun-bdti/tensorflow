/* Copyright 2021 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include <iostream>
#include "tensorflow/lite/kernels/internal/reference/gather.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/internal/tensor_ctypes.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/micro_utils.h"

namespace tflite {
namespace {

constexpr int kInputTensor = 0;
constexpr int kInputPositions = 1;
constexpr int kOutputTensor = 0;

template <typename T, typename CoordsT = int32>
inline void Gather(const tflite::GatherParams& op_params,
                   const RuntimeShape& input_shape, const T* input_data,
                   const RuntimeShape& coords_shape, const CoordsT* coords_data,
                   const RuntimeShape& output_shape, T* output_data) {
  ruy::profiler::ScopeLabel label("Gather");
  int axis = op_params.axis;
  if (axis < 0) {
    axis += input_shape.DimensionsCount();
  }
  TFLITE_DCHECK_GE(axis, 0);
  TFLITE_DCHECK_LT(axis, input_shape.DimensionsCount());
  const int axis_size = input_shape.Dims(axis);
  const int coords_count = coords_shape.FlatSize();

  int outer_size = 1;
  for (int i = 0; i < axis; ++i) {
    outer_size *= input_shape.Dims(i);
  }

  int inner_size = 1;
  for (int i = axis + 1; i < input_shape.DimensionsCount(); ++i) {
    inner_size *= input_shape.Dims(i);
  }

  for (int outer = 0; outer < outer_size; ++outer) {
    for (int i = 0; i < coords_count; ++i) {
      TFLITE_DCHECK_GE(coords_data[i], 0);
      TFLITE_DCHECK_LT(coords_data[i], axis_size);
      // TODO(rsun): replace memcpy with a for loop
      std::memcpy(
          output_data + (outer * coords_count + i) * inner_size,
          input_data + (outer * axis_size + coords_data[i]) * inner_size,
          sizeof(T) * inner_size);
    }
  }
}

template <typename InputT, typename CoordsT>
TfLiteStatus Gather(const TfLiteGatherParams* params, const TfLiteEvalTensor* input,
                    const TfLiteEvalTensor* positions, TfLiteEvalTensor* output) {
  std::cout << "in gather Gather()" << std::endl;
  tflite::GatherParams op_params;
  op_params.axis = params->axis;
  const RuntimeShape input_shape = tflite::micro::GetTensorShape(input);
  const InputT* input_data = tflite::micro::GetTensorData<InputT>(input);
  const RuntimeShape coord_shape = tflite::micro::GetTensorShape(positions);
  const CoordsT* coord_data = tflite::micro::GetTensorData<CoordsT>(positions);
  const RuntimeShape output_shape = tflite::micro::GetTensorShape(output);
  InputT* output_data = tflite::micro::GetTensorData<InputT>(output);

  std::cout << "launching reference op Gather()" << std::endl;
  reference_ops::Gather(op_params, input_shape, input_data, coord_shape,
                        coord_data, output_shape, output_data);
  std::cout << "end of gather Gather() OK" << std::endl;
  return kTfLiteOk;
}

TfLiteStatus Prepare(TfLiteContext* context, TfLiteNode* node) {
  std::cout << "in gather Prepare()" << std::endl;
  TF_LITE_ENSURE_EQ(context, NumInputs(node), 2);
  TF_LITE_ENSURE_EQ(context, NumOutputs(node), 1);

  const auto* params =
      reinterpret_cast<const TfLiteGatherParams*>(node->builtin_data);
  const TfLiteTensor* input;
  TF_LITE_ENSURE_OK(context, GetInputSafe(context, node, kInputTensor, &input));
  const TfLiteTensor* positions;
  TF_LITE_ENSURE_OK(context,
                    GetInputSafe(context, node, kInputPositions, &positions));
  TfLiteTensor* output;
  TF_LITE_ENSURE_OK(context,
                    GetOutputSafe(context, node, kOutputTensor, &output));

  switch (positions->type) {
    case kTfLiteInt64:
    case kTfLiteInt32:
      break;
    default:
      TF_LITE_KERNEL_LOG(context,
                         "Positions of type '%s' are not supported by gather.",
                         TfLiteTypeGetName(positions->type));
      return kTfLiteError;
  }

  // Assign to output the input type.
  output->type = input->type;

  // Check conditions for different types.
  switch (input->type) {
    case kTfLiteFloat32:
    case kTfLiteInt8:
      break;
    default:
      TF_LITE_KERNEL_LOG(context, "Type '%s' is not supported by gather.",
                         TfLiteTypeGetName(input->type));
      return kTfLiteError;
  }

  int axis = params->axis;
  if (axis < 0) {
    axis += NumDimensions(input);
  }
  TF_LITE_ENSURE(context, 0 <= axis && axis < NumDimensions(input));

  TfLiteIntArray* output_shape = output->dims;
  output_shape->size = NumDimensions(input) + NumDimensions(positions) - 1;
  int output_index = 0;
  for (int i = 0; i < axis; ++i) {
    output_shape->data[output_index++] = input->dims->data[i];
  }
  for (int i = 0; i < positions->dims->size; ++i) {
    output_shape->data[output_index++] = positions->dims->data[i];
  }
  for (int i = axis + 1; i < input->dims->size; ++i) {
    output_shape->data[output_index++] = input->dims->data[i];
  }
  std::cout << "end of gather Prepare() OK" << std::endl;
  return kTfLiteOk;
}

TfLiteStatus Eval(TfLiteContext* context, TfLiteNode* node) {
  std::cout << "in gather Eval()" << std::endl;
  const auto* params =
      reinterpret_cast<const TfLiteGatherParams*>(node->builtin_data);
  const TfLiteEvalTensor* input =
      tflite::micro::GetEvalInput(context, node, kInputTensor);
  const TfLiteEvalTensor* positions =
      tflite::micro::GetEvalInput(context, node, kInputPositions);
  TfLiteEvalTensor* output =
      tflite::micro::GetEvalOutput(context, node, kOutputTensor);

  if (positions->type == kTfLiteInt32) {
    switch (input->type) {
      case kTfLiteFloat32:
        return Gather<float, int32_t>(params, input, positions, output);
      case kTfLiteInt8:
        return Gather<int8_t, int32_t>(params, input, positions, output);
      default:
        TF_LITE_KERNEL_LOG(context, "Type '%s' is not supported by gather.",
                           TfLiteTypeGetName(input->type));
        return kTfLiteError;
    }
  }
  if (positions->type == kTfLiteInt64) {
    switch (input->type) {
      case kTfLiteFloat32:
        return Gather<float, int64_t>(params, input, positions, output);
      case kTfLiteInt8:
        return Gather<int8_t, int64_t>(params, input, positions, output);
      default:
        TF_LITE_KERNEL_LOG(context, "Type '%s' is not supported by gather.",
                           TfLiteTypeGetName(input->type));
        return kTfLiteError;
    }
  }
  std::cout << "end of gather Eval() OK" << std::endl;
  return kTfLiteOk;
}
}  // namespace

TfLiteRegistration Register_GATHER() {
  return {/*init=*/nullptr,
          /*free=*/nullptr,
          /*prepare=*/Prepare,
          /*invoke=*/Eval,
          /*profiling_string=*/nullptr,
          /*builtin_code=*/0,
          /*custom_name=*/nullptr,
          /*version=*/0};
}

}  // namespace tflite
