// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "pipeline_manager.h"
#include "src/lib/syslog/cpp/logger.h"
#include "stream_pipeline_info.h"
namespace camera {

constexpr auto TAG = "camera_controller";

const char* ToConfigFileName(const camera::GdcConfig& config_type) {
  switch (config_type) {
    case GdcConfig::MONITORING_360p:
      return "config_1152x1440_to_512x384_Crop_Rotate.bin";
    case GdcConfig::MONITORING_480p:
      return "config_1152x1440_to_720x540_Crop_Rotate.bin";
    case GdcConfig::MONITORING_720p:
      return "config_1152x1440_to_1152x864_Crop_Rotate.bin";
    case GdcConfig::MONITORING_ML:
      return "config_001_2176x2720-to-640x512-RS-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE:
      return "config_002_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_EXTENDED_FOV:
      return "config_003_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_ML:
      return "config_001_2240x1792-to-640x512-S-YUV420SemiPlanar.bin";
    case GdcConfig::INVALID:
    default:
      return nullptr;
  }
}

fit::result<gdc_config_info, zx_status_t> PipelineManager::LoadGdcConfiguration(
    const camera::GdcConfig& config_type) {
  if (config_type == GdcConfig::INVALID) {
    FX_LOGST(ERROR, TAG) << "Invalid GDC configuration type";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  gdc_config_info info;
  size_t size;
  auto status = load_firmware(device_, ToConfigFileName(config_type), &info.config_vmo, &size);
  if (status != ZX_OK || size == 0) {
    FX_PLOGST(ERROR, TAG, status) << "Failed to load the GDC firmware";
    return fit::error(status);
  }
  info.size = size;
  return fit::ok(std::move(info));
}

fit::result<ProcessNode*, zx_status_t> PipelineManager::CreateGdcNode(
    StreamCreationData* info, ProcessNode* parent_node, const InternalConfigNode& internal_gdc_node) {
  auto& input_buffers_hlcpp = parent_node->output_buffer_collection();
  auto result = GetBuffers(internal_gdc_node, info, parent_node);
  if (result.is_error()) {
    FX_LOGST(ERROR, TAG) << "Failed to get buffers";
    return fit::error(result.error());
  }

  auto output_buffers_hlcpp = std::move(result.value());

  BufferCollectionHelper output_buffer_collection_helper(output_buffers_hlcpp);
  BufferCollectionHelper input_buffer_collection_helper(input_buffers_hlcpp);

  // Convert the formats to C type
  std::vector<fuchsia_sysmem_ImageFormat_2> output_image_formats_c;
  for (uint32_t i = 0; i < internal_gdc_node.image_formats.size(); i++) {
    auto image_format_hlcpp = internal_gdc_node.image_formats[i];
    output_image_formats_c.push_back(ConvertHlcppImageFormat2toCType(&image_format_hlcpp));
  }

  // GDC only supports one input format and multiple output format at the
  // moment. So we take the first format from the previous node.
  // All existing usecases we support have only 1 format going into GDC.
  auto input_image_formats_c =
      ConvertHlcppImageFormat2toCType(&parent_node->output_image_formats()[0]);

  // Get the GDC configurations loaded
  std::vector<gdc_config_info> config_vmos_info;
  for (uint32_t i = 0; i < internal_gdc_node.gdc_info.config_type.size(); i++) {
    auto gdc_config = LoadGdcConfiguration(internal_gdc_node.gdc_info.config_type[i]);
    if (gdc_config.is_error()) {
      FX_LOGST(ERROR, TAG) << "Failed to load GDC configuration";
      return fit::error(gdc_config.error());
    }
    config_vmos_info.push_back(gdc_config.value());
  }

  auto cleanup = fbl::MakeAutoCall([config_vmos_info]() {
    for (auto info : config_vmos_info) {
      ZX_ASSERT_MSG(ZX_OK == zx_handle_close(info.config_vmo), "Failed to free up Config VMOs");
    }
  });

  // Create GDC Node
  auto gdc_node = std::make_unique<camera::ProcessNode>(
      gdc_, internal_gdc_node.type, parent_node, internal_gdc_node.image_formats,
      std::move(output_buffers_hlcpp), info->stream_config->properties.stream_type(),
      internal_gdc_node.supported_streams);
  if (!gdc_node) {
    FX_LOGST(ERROR, TAG) << "Failed to create GDC node";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  // Initialize the GDC to get a unique task index
  uint32_t gdc_task_index;
  auto status = gdc_.InitTask(input_buffer_collection_helper.GetC(),
                              output_buffer_collection_helper.GetC(), &input_image_formats_c,
                              output_image_formats_c.data(), output_image_formats_c.size(),
                              info->image_format_index, config_vmos_info.data(),
                              config_vmos_info.size(), gdc_node->hw_accelerator_frame_callback(),
                              gdc_node->hw_accelerator_res_callback(), &gdc_task_index);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, TAG, status) << "Failed to initialize GDC";
    return fit::error(status);
  }

  gdc_node->set_task_index(gdc_task_index);

  // Add child node info.
  ChildNodeInfo child_info;
  child_info.child_node = std::move(gdc_node);
  child_info.output_frame_rate = internal_gdc_node.output_frame_rate;
  auto return_value = fit::ok(child_info.child_node.get());

  parent_node->AddChildNodeInfo(std::move(child_info));
  return return_value;
}

}  // namespace camera
