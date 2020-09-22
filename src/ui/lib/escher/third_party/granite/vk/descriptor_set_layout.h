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
// - vulkan/descriptor_set.hpp

#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_DESCRIPTOR_SET_LAYOUT_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_DESCRIPTOR_SET_LAYOUT_H_

#include <vulkan/vulkan.hpp>

#include "src/ui/lib/escher/util/debug_print.h"

namespace escher {
namespace impl {

// The fields are bitmaps where each bit corresponds to a binding index within
// the set.  Therefore, there can be at most 32 descriptor bindings per set.
// Note that a bit can only be set in one of the fields; in other words for
// any pair of masks, (mask1 & mask2) must equal zero.  |fp_mask| is the
// exception; it tracks whether image formats are floating point or not.
//
// NOTE: When interpreting DescriptorSetLayouts, various code assumes that
// uniform buffers use dynamic offsets and storage buffers use static offsets.
//
// TODO(fxbug.dev/23921): Consider allowing both static/dynamic offsets for both storage
// and uniform buffers.
struct DescriptorSetLayout {
  uint32_t sampled_image_mask = 0;
  uint32_t storage_image_mask = 0;
  uint32_t uniform_buffer_mask = 0;
  uint32_t storage_buffer_mask = 0;
  uint32_t sampled_buffer_mask = 0;
  uint32_t input_attachment_mask = 0;
  uint32_t fp_mask = 0;
  vk::ShaderStageFlags stages;

  bool operator==(const DescriptorSetLayout& other) const {
    return sampled_image_mask == other.sampled_image_mask &&
           storage_image_mask == other.storage_image_mask &&
           uniform_buffer_mask == other.uniform_buffer_mask &&
           storage_buffer_mask == other.storage_buffer_mask &&
           sampled_buffer_mask == other.sampled_buffer_mask &&
           input_attachment_mask == other.input_attachment_mask && fp_mask == other.fp_mask &&
           stages == other.stages;
  }

  // Return false if the layout is invalid in any way (multiple descriptors
  // sharing the same binding index, etc).
  bool IsValid();
};

}  // namespace impl

ESCHER_DEBUG_PRINTABLE(impl::DescriptorSetLayout);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_GRANITE_VK_DESCRIPTOR_SET_LAYOUT_H_
