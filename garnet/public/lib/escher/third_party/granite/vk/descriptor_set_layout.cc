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

#include "lib/escher/third_party/granite/vk/descriptor_set_layout.h"

#include "src/lib/fxl/logging.h"

namespace escher {
namespace impl {

bool DescriptorSetLayout::IsValid() {
  uint32_t seen_bits = sampled_image_mask;
  uint32_t conflicts = (seen_bits & storage_image_mask);
  seen_bits |= storage_image_mask;
  conflicts |= (seen_bits & uniform_buffer_mask);
  seen_bits |= uniform_buffer_mask;
  conflicts |= (seen_bits & storage_buffer_mask);
  seen_bits |= storage_buffer_mask;
  conflicts |= (seen_bits & sampled_buffer_mask);
  seen_bits |= sampled_buffer_mask;
  conflicts |= (seen_bits & input_attachment_mask);

  if (conflicts != 0) {
    FXL_LOG(WARNING) << "multiple descriptors in set share binding indices: "
                     << std::hex << conflicts;
    return false;
  }
  return true;
}

}  // namespace impl
}  // namespace escher
