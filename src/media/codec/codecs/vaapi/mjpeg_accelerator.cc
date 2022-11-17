// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mjpeg_accelerator.h"

#include <va/va.h>

#include "codec_adapter_vaapi_decoder.h"
#include "safemath/safe_conversions.h"
#include "vaapi_utils.h"

VaapiJpegPicture::VaapiJpegPicture(std::shared_ptr<VASurface> va_surface)
    : va_surface_(va_surface) {}

VaapiJpegPicture::~VaapiJpegPicture() = default;

MJPEGAccelerator::MJPEGAccelerator(CodecAdapterVaApiDecoder* adapter) : adapter_(adapter) {
  FX_DCHECK(adapter_);
}

MJPEGAccelerator::~MJPEGAccelerator() = default;

// Set picture parameters.
std::shared_ptr<media::JPEGPicture> MJPEGAccelerator::CreateJPEGPicture() {
  auto surface = adapter_->GetVASurface();
  auto surface_ptr = std::make_shared<VaapiJpegPicture>(surface);
  return surface_ptr;
}

MJPEGAccelerator::Status MJPEGAccelerator::SubmitDecode(
    std::shared_ptr<media::JPEGPicture> picture, const media::JpegParseResult& parse_result) {
  // Populate the picture parameters
  VAPictureParameterBufferJPEGBaseline pic_param{};
  PopulatePictureParameterBuffer(picture->frame_header(), pic_param);

  // Populate the IQ matrix
  VAIQMatrixBufferJPEGBaseline matrix_buffer{};
  PopulateIQMatrix(parse_result.q_table, matrix_buffer);

  // Populate huffman table
  VAHuffmanTableBufferJPEGBaseline huffman_table{};
  PopulateHuffmanTable(parse_result.dc_table, parse_result.ac_table, huffman_table);

  // Populate slice parameters
  VASliceParameterBufferJPEGBaseline slice_param{};
  PopulateSliceParameters(parse_result, slice_param);

  VAStatus status;
  VADisplay display = VADisplayWrapper::GetSingleton()->display();

  VABufferID pic_params_buffer_id;
  status = vaCreateBuffer(display, adapter_->context_id(), VAPictureParameterBufferType,
                          sizeof(pic_param), 1, &pic_param, &pic_params_buffer_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for pic_param failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }
  ScopedBufferID pic_params_buffer(pic_params_buffer_id);

  VABufferID iq_matrix_buffer_id;
  status = vaCreateBuffer(display, adapter_->context_id(), VAIQMatrixBufferType,
                          sizeof(matrix_buffer), 1, &matrix_buffer, &iq_matrix_buffer_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for matrix_buffer failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }
  ScopedBufferID iq_matrix_buffer(iq_matrix_buffer_id);

  VABufferID huffman_table_buffer_id;
  status = vaCreateBuffer(display, adapter_->context_id(), VAHuffmanTableBufferType,
                          sizeof(huffman_table), 1, &huffman_table, &huffman_table_buffer_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for huffman_table failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }
  ScopedBufferID huffman_table_buffer(huffman_table_buffer_id);

  VABufferID slice_param_buffer_id;
  status = vaCreateBuffer(display, adapter_->context_id(), VASliceParameterBufferType,
                          sizeof(slice_param), 1, &slice_param, &slice_param_buffer_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for slice_param failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }
  ScopedBufferID slice_param_buffer(slice_param_buffer_id);

  VABufferID jpeg_data_buffer_id;
  status = vaCreateBuffer(display, adapter_->context_id(), VASliceDataBufferType,
                          static_cast<unsigned int>(parse_result.data_size), 1,
                          reinterpret_cast<uint8_t*>(const_cast<char*>(parse_result.data)),
                          &jpeg_data_buffer_id);

  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for jpeg_data_buffer_id failed",
            KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }
  ScopedBufferID jpeg_data_buffer(jpeg_data_buffer_id);

  auto va_surface_id = static_cast<VaapiJpegPicture*>(picture.get())->GetVASurfaceID();
  status = vaBeginPicture(display, adapter_->context_id(), va_surface_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "BeginPicture failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  std::vector<VABufferID> buffers{pic_params_buffer.id(), iq_matrix_buffer.id(),
                                  huffman_table_buffer.id(), slice_param_buffer.id(),
                                  jpeg_data_buffer.id()};
  status = vaRenderPicture(display, adapter_->context_id(), buffers.data(),
                           static_cast<int>(buffers.size()));
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "RenderPicture failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  status = vaEndPicture(display, adapter_->context_id());
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "EndPicture failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  return Status::kOk;
}

bool MJPEGAccelerator::OutputPicture(std::shared_ptr<media::JPEGPicture> picture) {
  auto va_surface = static_cast<VaapiJpegPicture*>(picture.get())->va_surface();
  return adapter_->ProcessOutput(va_surface, picture->bitstream_id());
}

void MJPEGAccelerator::PopulatePictureParameterBuffer(
    const media::JpegFrameHeader& frame_header, VAPictureParameterBufferJPEGBaseline& pic_param) {
  pic_param.picture_width = frame_header.coded_width;
  pic_param.picture_height = frame_header.coded_height;
  pic_param.num_components = frame_header.num_components;

  for (int component_idx = 0; component_idx < pic_param.num_components; component_idx += 1) {
    const auto& header_comp = frame_header.components[component_idx];
    auto& pic_comp = pic_param.components[component_idx];

    pic_comp.component_id = header_comp.id;
    pic_comp.h_sampling_factor = header_comp.horizontal_sampling_factor;
    pic_comp.v_sampling_factor = header_comp.vertical_sampling_factor;
    pic_comp.quantiser_table_selector = header_comp.quantization_table_selector;
  }
}

void MJPEGAccelerator::PopulateIQMatrix(
    const media::JpegQuantizationTable q_table[media::kJpegMaxQuantizationTableNum],
    VAIQMatrixBufferJPEGBaseline matrix_buffer) {
  static_assert(media::kJpegMaxQuantizationTableNum ==
                    std::extent<decltype(matrix_buffer.load_quantiser_table)>(),
                "max number of quantization table mismatched");
  static_assert(sizeof(matrix_buffer.quantiser_table[0]) == sizeof(q_table[0].value),
                "number of quantization entries mismatched");
  for (size_t i = 0; i < media::kJpegMaxQuantizationTableNum; i++) {
    if (!q_table[i].valid) {
      continue;
    }
    matrix_buffer.load_quantiser_table[i] = 1;
    for (size_t j = 0; j < std::size(q_table[i].value); j++) {
      matrix_buffer.quantiser_table[i][j] = q_table[i].value[j];
    }
  }
}

void MJPEGAccelerator::PopulateHuffmanTable(
    const media::JpegHuffmanTable dc_table[media::kJpegMaxHuffmanTableNumBaseline],
    const media::JpegHuffmanTable ac_table[media::kJpegMaxHuffmanTableNumBaseline],
    VAHuffmanTableBufferJPEGBaseline& huffman_table) {
  // Use default huffman tables if not specified in header.
  bool has_huffman_table = false;
  for (size_t i = 0; i < media::kJpegMaxHuffmanTableNumBaseline; i++) {
    if (dc_table[i].valid || ac_table[i].valid) {
      has_huffman_table = true;
      break;
    }
  }

  if (!has_huffman_table) {
    dc_table = media::kDefaultDcTable;
    ac_table = media::kDefaultAcTable;
  }

  static_assert(media::kJpegMaxHuffmanTableNumBaseline ==
                    std::extent<decltype(huffman_table.load_huffman_table)>(),
                "max number of huffman table mismatched");
  static_assert(
      sizeof(huffman_table.huffman_table[0].num_dc_codes) == sizeof(dc_table[0].code_length),
      "size of huffman table code length mismatch");
  static_assert(
      sizeof(huffman_table.huffman_table[0].dc_values[0]) == sizeof(dc_table[0].code_value[0]),
      "size of huffman table code value mismatch");
  for (size_t i = 0; i < media::kJpegMaxHuffmanTableNumBaseline; i++) {
    if (!dc_table[i].valid || !ac_table[i].valid) {
      continue;
    }

    huffman_table.load_huffman_table[i] = 1;
    std::memcpy(huffman_table.huffman_table[i].num_dc_codes, dc_table[i].code_length,
                sizeof(huffman_table.huffman_table[i].num_dc_codes));
    std::memcpy(huffman_table.huffman_table[i].dc_values, dc_table[i].code_value,
                sizeof(huffman_table.huffman_table[i].dc_values));
    std::memcpy(huffman_table.huffman_table[i].num_ac_codes, ac_table[i].code_length,
                sizeof(huffman_table.huffman_table[i].num_ac_codes));
    std::memcpy(huffman_table.huffman_table[i].ac_values, ac_table[i].code_value,
                sizeof(huffman_table.huffman_table[i].ac_values));
  }
}

void MJPEGAccelerator::PopulateSliceParameters(const media::JpegParseResult& parse_result,
                                               VASliceParameterBufferJPEGBaseline& slice_param) {
  slice_param.slice_data_size = safemath::checked_cast<uint32_t>(parse_result.data_size);
  slice_param.slice_data_offset = 0;
  slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
  slice_param.slice_horizontal_position = 0;
  slice_param.slice_vertical_position = 0;
  slice_param.num_components = parse_result.scan.num_components;
  for (int i = 0; i < slice_param.num_components; i++) {
    slice_param.components[i].component_selector =
        parse_result.scan.components[i].component_selector;
    slice_param.components[i].dc_table_selector = parse_result.scan.components[i].dc_selector;
    slice_param.components[i].ac_table_selector = parse_result.scan.components[i].ac_selector;
  }
  slice_param.restart_interval = parse_result.restart_interval;

  // Cast to int to prevent overflow.
  int max_h_factor = parse_result.frame_header.components[0].horizontal_sampling_factor;
  int max_v_factor = parse_result.frame_header.components[0].vertical_sampling_factor;
  int mcu_cols = parse_result.frame_header.coded_width / (max_h_factor * 8);
  FX_DCHECK(mcu_cols == 0);
  int mcu_rows = parse_result.frame_header.coded_height / (max_v_factor * 8);
  FX_DCHECK(mcu_rows == 0);
  slice_param.num_mcus = mcu_rows * mcu_cols;
}
