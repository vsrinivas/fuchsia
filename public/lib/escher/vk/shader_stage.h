// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_SHADER_STAGE_H_
#define LIB_ESCHER_VK_SHADER_STAGE_H_

#include <vulkan/vulkan.hpp>

#include "lib/escher/util/debug_print.h"

namespace escher {

enum class ShaderStage : uint8_t {
  kVertex = 0,
  kTessellationControl = 1,
  kTessellationEvaluation = 2,
  kGeometry = 3,
  kFragment = 4,
  kCompute = 5,
  kEnumCount
};
ESCHER_DEBUG_PRINTABLE(ShaderStage);

inline vk::ShaderStageFlags ShaderStageToFlags(ShaderStage stage) {
  switch (stage) {
    case ShaderStage::kVertex:
      return vk::ShaderStageFlagBits::eVertex;
    case ShaderStage::kTessellationControl:
      return vk::ShaderStageFlagBits::eTessellationControl;
    case ShaderStage::kTessellationEvaluation:
      return vk::ShaderStageFlagBits::eTessellationEvaluation;
    case ShaderStage::kGeometry:
      return vk::ShaderStageFlagBits::eGeometry;
    case ShaderStage::kFragment:
      return vk::ShaderStageFlagBits::eFragment;
    case ShaderStage::kCompute:
      return vk::ShaderStageFlagBits::eCompute;
    case ShaderStage::kEnumCount:
      return vk::ShaderStageFlags();
  }
}

}  // namespace escher

#endif  // LIB_ESCHER_VK_SHADER_STAGE_H_
