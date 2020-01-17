// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_UTILS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_UTILS_H_

#include <stdbool.h>
#include <vulkan/vulkan.h>

// Parse a --device option argument if available.
// If |option| is null, |*vendor_id| and |*device_id| are set to 0.
// Otherwise, the string is parsed for a valid vendor,device pair.
// Returns true on success. On failure, print an error to stderr then return
// false.
extern bool
parseDeviceOption(const char * option, uint32_t * vendor_id, uint32_t * device_id);

// Parse a --window option argument if available.
// If |option| is null, |*window_width| and |*window_height| are set to
// |default_width| and |default_height| respectively. Otherwise, the string is
// parsed for a valid "<width>x<height>" dimension.
// Returns true on success. On failure, print an error to stderr then return
// false.
extern bool
parseWindowOption(const char * option,
                  uint32_t     default_width,
                  uint32_t     default_height,
                  uint32_t *   window_width,
                  uint32_t *   window_height);

// Parse a --format option argument if available.
// If |option| is null, |*format| will be set to VK_FORMAT_UNDEFINED.
// Otherwise, the string is parsed for a valid format name.
// Returns true on success. On failure, print an error to stderr (describing
// the set of valid format name values) then return false.
extern bool
parseFormatOption(const char * option, VkFormat * format);

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_UTILS_H_
