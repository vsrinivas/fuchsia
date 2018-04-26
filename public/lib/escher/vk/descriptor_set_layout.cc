// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/vk/descriptor_set_layout.h"

#include "lib/fxl/logging.h"

namespace escher {

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

}  // namespace escher
