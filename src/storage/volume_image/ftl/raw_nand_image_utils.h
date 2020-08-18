// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FTL_RAW_NAND_IMAGE_UTILS_H_
#define SRC_STORAGE_VOLUME_IMAGE_FTL_RAW_NAND_IMAGE_UTILS_H_

#include <lib/fit/result.h>

#include <cstdint>

#include <fbl/algorithm.h>
#include <fbl/span.h>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Returns the adjusted page size of a RawNandImage with the given |options|.
constexpr uint64_t RawNandImageGetAdjustedPageSize(const RawNandOptions& options) {
  return options.page_size + options.oob_bytes_size;
}

// Returns the adjusted erase block size size of a RawNandImage with the given |options|.
constexpr uint64_t RawNandImageGetAdjustedEraseBlockSize(const RawNandOptions& options) {
  return options.pages_per_block * RawNandImageGetAdjustedPageSize(options);
}

// Returns the offset in bytes of |page_number| page from the start, with a known |page_size| and
// |oob_bytes_size|.
constexpr uint64_t RawNandImageGetPageOffset(uint64_t page_number, const RawNandOptions& options) {
  return page_number * RawNandImageGetAdjustedPageSize(options);
}

// Returns the offset of the first erase block that start after or at |start_offset|.
constexpr uint64_t RawNandImageGetNextEraseBlockOffset(uint64_t start_offset,
                                                       const RawNandOptions& options) {
  return fbl::round_up(start_offset, RawNandImageGetAdjustedEraseBlockSize(options));
}

// Writes a block of size |oob_bytes.size() + page_content.size()| into |writer| at |offset|.
inline fit::result<void, std::string> RawNandImageWritePage(fbl::Span<const uint8_t> page_content,
                                                            fbl::Span<const uint8_t> oob_bytes,
                                                            uint64_t offset, Writer* writer) {
  auto result = writer->Write(offset, page_content);
  if (result.is_error()) {
    return result.take_error_result();
  }
  return writer->Write(offset + page_content.size(), oob_bytes);
}

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_FTL_RAW_NAND_IMAGE_UTILS_H_
