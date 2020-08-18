// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FTL_OPTIONS_H_
#define SRC_STORAGE_VOLUME_IMAGE_FTL_OPTIONS_H_

#include <cstdint>

namespace storage::volume_image {

// Set of options for generating a Raw Nand Image.
struct RawNandOptions {
  // Page size as perceived by the FTL.
  uint64_t page_size = 0;

  // Number of pages in the device.
  uint32_t page_count = 0;

  // Number of pages per erase block unit.
  uint32_t pages_per_block = 0;

  // Number of OOB bytes expected.
  uint8_t oob_bytes_size = 0;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FTL_OPTIONS_H_
