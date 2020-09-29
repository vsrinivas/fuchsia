// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/debug_print.h"

#include <ios>

#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/geometry/transform.h"
#include "src/ui/lib/escher/impl/model_pipeline_spec.h"
#include "src/ui/lib/escher/paper/paper_renderer_config.h"
#include "src/ui/lib/escher/scene/camera.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/lib/escher/third_party/granite/vk/command_buffer_pipeline_state.h"
#include "src/ui/lib/escher/third_party/granite/vk/descriptor_set_layout.h"
#include "src/ui/lib/escher/third_party/granite/vk/pipeline_layout.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass_info.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/lib/escher/vk/shader_module.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"

namespace escher {

std::ostream& operator<<(std::ostream& str, const Transform& transform) {
  return str << "Transform[t: " << transform.translation << " s: " << transform.scale
             << " r: " << transform.rotation << " a: " << transform.anchor << "]";
}

std::ostream& operator<<(std::ostream& str, const mat2& m) {
  str << "mat2[";
  for (int y = 0; y < 2; ++y) {
    str << std::endl;
    for (int x = 0; x < 2; ++x) {
      str << " " << m[x][y];
    }
  }
  return str << " ]";
}

std::ostream& operator<<(std::ostream& str, const mat4& m) {
  str << "mat4[";
  for (int y = 0; y < 4; ++y) {
    str << std::endl;
    for (int x = 0; x < 4; ++x) {
      str << " " << m[x][y];
    }
  }
  return str << " ]";
}

std::ostream& operator<<(std::ostream& str, const vec2& v) {
  return str << "(" << v[0] << ", " << v[1] << ")";
}

std::ostream& operator<<(std::ostream& str, const vec3& v) {
  return str << "(" << v[0] << ", " << v[1] << ", " << v[2] << ")";
}

std::ostream& operator<<(std::ostream& str, const vec4& v) {
  return str << "(" << v[0] << ", " << v[1] << ", " << v[2] << ", " << v[3] << ")";
}

std::ostream& operator<<(std::ostream& str, const quat& q) {
  return str << "(" << q.x << ", " << q.y << ", " << q.z << ", " << q.w << ")";
}

std::ostream& operator<<(std::ostream& str, const plane2& p) {
  return str << "plane2[dir:" << p.dir() << " dist:" << p.dist() << "]";
}

std::ostream& operator<<(std::ostream& str, const plane3& p) {
  return str << "plane3[dir:" << p.dir() << " dist:" << p.dist() << "]";
}

std::ostream& operator<<(std::ostream& str, const Rectangle2D& r) {
  return str << "Rectangle2D[origin:" << r.origin << " extent:" << r.extent << " clockwise_uvs:["
             << r.clockwise_uvs[0] << "," << r.clockwise_uvs[1] << "," << r.clockwise_uvs[2] << ","
             << r.clockwise_uvs[3] << "]]";
}

std::ostream& operator<<(std::ostream& str, const MeshAttribute& attr) {
  switch (attr) {
    case MeshAttribute::kPosition2D:
      str << "kPosition2D";
      break;
    case MeshAttribute::kPosition3D:
      str << "kPosition3D";
      break;
    case MeshAttribute::kPositionOffset:
      str << "kPositionOffset";
      break;
    case MeshAttribute::kUV:
      str << "kUV";
      break;
    case MeshAttribute::kPerimeterPos:
      str << "kPerimeterPos";
      break;
    case MeshAttribute::kBlendWeight1:
      str << "kBlendWeight1";
      break;
    case MeshAttribute::kStride:
      str << "kStride";
      break;
  }
  return str;
}

std::ostream& operator<<(std::ostream& str, const MeshAttributes& attributes) {
  static_assert(uint32_t(MeshAttribute::kStride) == (1 << 6), "missing enum");

  constexpr std::array<MeshAttribute, 6> all_flags = {
      {MeshAttribute::kPosition2D, MeshAttribute::kPosition3D, MeshAttribute::kPositionOffset,
       MeshAttribute::kUV, MeshAttribute::kPerimeterPos, MeshAttribute::kBlendWeight1}};

  bool has_flag = false;  // has a flag already been seen?
  for (auto flag : all_flags) {
    if (attributes & flag) {
      // Put a pipe after the previous flag, if there is one.
      if (has_flag) {
        str << "|";
      } else {
        has_flag = true;
      }
      str << flag;
    }
  }

  return str;
}

std::ostream& operator<<(std::ostream& str, const MeshSpec& spec) {
  str << "MeshSpec[";

  bool any_attributes = false;
  for (uint32_t i = 0; i < VulkanLimits::kNumVertexBuffers; ++i) {
    if (spec.attribute_count(i) > 0) {
      if (any_attributes) {
        str << ", ";
      }
      any_attributes = true;
      str << i << ":" << spec.attributes[i];
    }
  }
  str << "]";
  return str;
}

std::ostream& operator<<(std::ostream& str,
                         const impl::ModelPipelineSpec::ClipperState& clipper_state) {
  using ClipperState = impl::ModelPipelineSpec::ClipperState;
  switch (clipper_state) {
    case ClipperState::kBeginClipChildren:
      str << "ClipperState::kBeginClipChildren";
      break;
    case ClipperState::kEndClipChildren:
      str << "ClipperState::kEndClipChildren";
      break;
    case ClipperState::kNoClipChildren:
      str << "ClipperState::kNoClipChildren";
      break;
  }
  return str;
}

std::ostream& operator<<(std::ostream& str, const impl::ModelPipelineSpec& spec) {
  str << "ModelPipelineSpec[" << spec.mesh_spec << ", clipper_state: " << spec.clipper_state
      << ", is_clippee: " << spec.is_clippee << ", has_material: " << spec.has_material
      << ", is_opaque: " << spec.is_opaque << "]";
  return str;
}

std::ostream& operator<<(std::ostream& str, const ImageInfo& info) {
  return str << "ImageInfo[" << info.width << "x" << info.height << " "
             << vk::to_string(info.format) << "  samples: " << info.sample_count << "]";
}

std::ostream& operator<<(std::ostream& str, const ViewingVolume& volume) {
  return str << "ViewingVolume[w:" << volume.width() << " h:" << volume.height()
             << " t:" << volume.top() << " b:" << volume.bottom() << "]";
}

std::ostream& operator<<(std::ostream& str, const BoundingBox& box) {
  if (box.is_empty()) {
    return str << "BoundingBox[empty]";
  } else {
    return str << "BoundingBox[min" << box.min() << ", max" << box.max() << "]";
  }
}

std::ostream& operator<<(std::ostream& str, const Camera& camera) {
  return str << "Camera[\ntransform: " << camera.transform()
             << "\nprojection: " << camera.projection() << "]";
}

std::ostream& operator<<(std::ostream& str, const impl::DescriptorSetLayout& layout) {
  return str << "DescriptorSetLayout[\n\tsampled_image_mask: " << std::hex
             << layout.sampled_image_mask << "\n\tstorage_image_mask: " << layout.storage_image_mask
             << "\n\tuniform_buffer_mask: " << layout.uniform_buffer_mask
             << "\n\tstorage_buffer_mask: " << layout.storage_buffer_mask
             << "\n\tsampled_buffer_mask: " << layout.sampled_buffer_mask
             << "\n\tinput_attachment_mask: " << layout.input_attachment_mask
             << "\n\tfp_mask: " << layout.fp_mask << "\n\t" << std::dec
             << vk::to_string(layout.stages) << "]";
}

std::ostream& operator<<(std::ostream& str, const impl::ShaderModuleResourceLayout& layout) {
  str << "ShaderModuleResourceLayout[\n\tattribute_mask: " << std::hex << layout.attribute_mask
      << "\n\trender_target_mask: " << layout.render_target_mask
      << "\n\tpush_constant_offset: " << layout.push_constant_offset
      << "\n\tpush_constant_range: " << layout.push_constant_range << std::dec;
  for (size_t i = 0; i < VulkanLimits::kNumDescriptorSets; ++i) {
    str << "\n\t" << i << ": " << layout.sets[i];
  }
  return str << "]";
}

std::ostream& operator<<(std::ostream& str, const ShaderStage& stage) {
  switch (stage) {
    case ShaderStage::kVertex:
      return str << "ShaderStage::kVertex";
    case ShaderStage::kTessellationControl:
      return str << "ShaderStage::kTessellationControl";
    case ShaderStage::kTessellationEvaluation:
      return str << "ShaderStage::kTessellationEvaluation";
    case ShaderStage::kGeometry:
      return str << "ShaderStage::kGeometry";
    case ShaderStage::kFragment:
      return str << "ShaderStage::kFragment";
    case ShaderStage::kCompute:
      return str << "ShaderStage::kCompute";
    case ShaderStage::kEnumCount:
      return str << "ShaderStage::kEnumCount(INVALID)";
  }
}

std::ostream& operator<<(std::ostream& str, const impl::PipelineLayoutSpec& spec) {
  str << "==============PipelineLayoutSpec[\n\tattribute_mask: " << std::hex
      << spec.attribute_mask() << "\n\trender_target_mask: " << spec.render_target_mask()
      << "\n\tnum_push_constant_ranges: " << spec.num_push_constant_ranges()
      << "\n\tdescriptor_set_mask: " << spec.descriptor_set_mask();
  ForEachBitIndex(spec.descriptor_set_mask(), [&](uint32_t index) {
    str << "\n=== index: " << index << " " << spec.descriptor_set_layouts(index);
  });

  return str << "\n]";
}

static const char* PaperRendererShadowTypeString(const PaperRendererShadowType& shadow_type) {
  switch (shadow_type) {
    case PaperRendererShadowType::kNone:
      return "kNone";
    case PaperRendererShadowType::kSsdo:
      return "kSsdo";
    case PaperRendererShadowType::kShadowMap:
      return "kShadowMap";
    case PaperRendererShadowType::kMomentShadowMap:
      return "kMomentShadowMap";
    case PaperRendererShadowType::kShadowVolume:
      return "kShadowVolume";
    case PaperRendererShadowType::kEnumCount:
      return "kEnumCount(INVALID)";
  }
}

std::ostream& operator<<(std::ostream& str, const PaperRendererShadowType& shadow_type) {
  return str << "PaperRendererShadowType::" << PaperRendererShadowTypeString(shadow_type);
}

std::ostream& operator<<(std::ostream& str, const PaperRendererConfig& config) {
  return str << "PaperRendererConfig[shadow_type:"
             << PaperRendererShadowTypeString(config.shadow_type)
             << " sample_count:" << unsigned{config.msaa_sample_count} << "]";
}

std::ostream& operator<<(std::ostream& str, const VulkanDeviceQueues::Caps& caps) {
  str << "Caps[\n  max_image_width: " << caps.max_image_width
      << "  max_image_height: " << caps.max_image_height << "\n  depth_stencil_formats:";
  for (auto& fmt : caps.depth_stencil_formats) {
    str << "\n    " << vk::to_string(fmt);
  }
  str << "\n  extensions:";
  for (auto& name : caps.extensions) {
    str << "\n    " << name;
  }
  str << "\n  enabled_features:";
#define PRINT_FEATURE(X)         \
  if (caps.enabled_features.X) { \
    str << "\n    " << #X;       \
  }
  PRINT_FEATURE(robustBufferAccess);
  PRINT_FEATURE(fullDrawIndexUint32);
  PRINT_FEATURE(imageCubeArray);
  PRINT_FEATURE(independentBlend);
  PRINT_FEATURE(geometryShader);
  PRINT_FEATURE(tessellationShader);
  PRINT_FEATURE(sampleRateShading);
  PRINT_FEATURE(dualSrcBlend);
  PRINT_FEATURE(logicOp);
  PRINT_FEATURE(multiDrawIndirect);
  PRINT_FEATURE(drawIndirectFirstInstance);
  PRINT_FEATURE(depthClamp);
  PRINT_FEATURE(depthBiasClamp);
  PRINT_FEATURE(fillModeNonSolid);
  PRINT_FEATURE(depthBounds);
  PRINT_FEATURE(wideLines);
  PRINT_FEATURE(largePoints);
  PRINT_FEATURE(alphaToOne);
  PRINT_FEATURE(multiViewport);
  PRINT_FEATURE(samplerAnisotropy);
  PRINT_FEATURE(textureCompressionETC2);
  PRINT_FEATURE(textureCompressionASTC_LDR);
  PRINT_FEATURE(textureCompressionBC);
  PRINT_FEATURE(occlusionQueryPrecise);
  PRINT_FEATURE(pipelineStatisticsQuery);
  PRINT_FEATURE(vertexPipelineStoresAndAtomics);
  PRINT_FEATURE(fragmentStoresAndAtomics);
  PRINT_FEATURE(shaderTessellationAndGeometryPointSize);
  PRINT_FEATURE(shaderImageGatherExtended);
  PRINT_FEATURE(shaderStorageImageExtendedFormats);
  PRINT_FEATURE(shaderStorageImageMultisample);
  PRINT_FEATURE(shaderStorageImageReadWithoutFormat);
  PRINT_FEATURE(shaderStorageImageWriteWithoutFormat);
  PRINT_FEATURE(shaderUniformBufferArrayDynamicIndexing);
  PRINT_FEATURE(shaderSampledImageArrayDynamicIndexing);
  PRINT_FEATURE(shaderStorageBufferArrayDynamicIndexing);
  PRINT_FEATURE(shaderStorageImageArrayDynamicIndexing);
  PRINT_FEATURE(shaderClipDistance);
  PRINT_FEATURE(shaderCullDistance);
  PRINT_FEATURE(shaderFloat64);
  PRINT_FEATURE(shaderInt64);
  PRINT_FEATURE(shaderInt16);
  PRINT_FEATURE(shaderResourceResidency);
  PRINT_FEATURE(shaderResourceMinLod);
  PRINT_FEATURE(sparseBinding);
  PRINT_FEATURE(sparseResidencyBuffer);
  PRINT_FEATURE(sparseResidencyImage2D);
  PRINT_FEATURE(sparseResidencyImage3D);
  PRINT_FEATURE(sparseResidency2Samples);
  PRINT_FEATURE(sparseResidency4Samples);
  PRINT_FEATURE(sparseResidency8Samples);
  PRINT_FEATURE(sparseResidency16Samples);
  PRINT_FEATURE(sparseResidencyAliased);
  PRINT_FEATURE(variableMultisampleRate);
  PRINT_FEATURE(inheritedQueries);
#undef PRINT_FEATURE

  return str << "\n]";
}

std::ostream& operator<<(std::ostream& str, const CommandBufferPipelineState::StaticState& state) {
  return str
         << "\n\tdepth_write: " << state.get_depth_write()
         << "\n\tdepth_test: " << state.get_depth_test()
         << "\n\tblend_enable: " << state.get_blend_enable()
         << "\n\tcull_mode: " << vk::to_string(state.get_cull_mode())
         << "\n\tfront_face: " << vk::to_string(state.get_front_face())
         << "\n\tdepth_bias_enable: " << state.get_depth_bias_enable()
         << "\n\tdepth_compare: " << vk::to_string(state.get_depth_compare())
         << "\n\tstencil_test: " << state.get_stencil_test()
         << "\n\tstencil_front_fail: " << vk::to_string(state.get_stencil_front_fail())
         << "\n\tstencil_front_pass: " << vk::to_string(state.get_stencil_front_pass())
         << "\n\tstencil_front_depth_fail: " << vk::to_string(state.get_stencil_front_depth_fail())
         << "\n\tstencil_front_compare_op: " << vk::to_string(state.get_stencil_front_compare_op())
         << "\n\tstencil_back_fail: " << vk::to_string(state.get_stencil_back_fail())
         << "\n\tstencil_back_pass: " << vk::to_string(state.get_stencil_back_pass())
         << "\n\tstencil_back_depth_fail: " << vk::to_string(state.get_stencil_back_depth_fail())
         << "\n\tstencil_back_compare_op: " << vk::to_string(state.get_stencil_back_compare_op())
         << "\n\talpha_to_coverage: " << state.get_alpha_to_coverage()
         << "\n\talpha_to_one: " << state.get_alpha_to_one()
         << "\n\tsample_shading: " << state.get_sample_shading()
         << "\n\tsrc_color_blend: " << vk::to_string(state.get_src_color_blend())
         << "\n\tdst_color_blend: " << vk::to_string(state.get_dst_color_blend())
         << "\n\tcolor_blend_op: " << vk::to_string(state.get_color_blend_op())
         << "\n\tsrc_alpha_blend: " << vk::to_string(state.get_src_alpha_blend())
         << "\n\tdst_alpha_blend: " << vk::to_string(state.get_dst_alpha_blend())
         << "\n\talpha_blend_op: " << vk::to_string(state.get_alpha_blend_op())
         << "\n\tprimitive_restart: " << state.get_primitive_restart()
         << "\n\tprimitive_topology: " << vk::to_string(state.get_primitive_topology())
         << "\n\twireframe: " << state.get_wireframe()
         << "\n\tcolor_write_mask: " << state.color_write_mask << "\n\tpadding: " << state.padding;
}

std::ostream& operator<<(std::ostream& str, const RenderPassInfo::AttachmentInfo& info) {
  return str << vk::to_string(info.format) << ", " << vk::to_string(info.swapchain_layout) << ", "
             << info.sample_count << ", " << (info.is_transient ? "true" : "false");
}

std::ostream& operator<<(std::ostream& str, const RenderPassInfo& rpi) {
  if (rpi.Validate()) {
    str << "RenderPassInfo[";
  } else {
    str << "RenderPassInfo[ (invalid: see errors above)";
  }
  str << "\n\tnum_subpasses: " << rpi.subpasses.size()
      << "\n\tnum_color_attachments: " << rpi.num_color_attachments
      << "\n\tclear/load/store attachments: 0x" << std::hex << rpi.clear_attachments << "/0x"
      << rpi.load_attachments << "/0x" << rpi.store_attachments << std::dec;
  for (size_t i = 0; i < rpi.num_color_attachments; ++i) {
    str << "\n\t\t[" << i << "]: " << rpi.color_attachment_infos[i];
  }
  if (rpi.depth_stencil_attachment_info.format != vk::Format::eUndefined) {
    str << "\n\tdepth_stencil_attachment: " << rpi.depth_stencil_attachment_info;
  }
  return str << "\n]";
}

}  // namespace escher
