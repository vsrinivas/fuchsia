/* Copyright (c) 2017 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

// Based on the following files from the Granite rendering engine:
// - vulkan/shader.hpp

#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_SHADER_MODULE_RESOURCE_LAYOUT_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_SHADER_MODULE_RESOURCE_LAYOUT_H_

#include "src/ui/lib/escher/third_party/granite/vk/descriptor_set_layout.h"
#include "src/ui/lib/escher/util/debug_print.h"
#include "src/ui/lib/escher/vk/vulkan_limits.h"

namespace escher {
namespace impl {

// Stores pipeline layout information that is obtained by reflecting on a
// ShaderModule's SPIR-V code.
struct ShaderModuleResourceLayout {
  uint32_t attribute_mask = 0;
  uint32_t render_target_mask = 0;
  uint32_t push_constant_offset = 0;
  uint32_t push_constant_range = 0;
  DescriptorSetLayout sets[VulkanLimits::kNumDescriptorSets];
};

}  // namespace impl

ESCHER_DEBUG_PRINTABLE(impl::ShaderModuleResourceLayout);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_SHADER_MODULE_RESOURCE_LAYOUT_H_
