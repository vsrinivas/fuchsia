// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ftl_raw_nand_image_writer.h"

#include <lib/fit/result.h>

#include <cstdint>
#include <iostream>
#include <tuple>
#include <type_traits>

#include <fbl/algorithm.h>

#include "src/storage/volume_image/ftl/ftl_image_internal.h"
#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/ftl/raw_nand_image.h"
#include "src/storage/volume_image/ftl/raw_nand_image_utils.h"

namespace storage::volume_image {

fit::result<std::tuple<FtlRawNandImageWriter, RawNandOptions>, std::string>
FtlRawNandImageWriter::Create(const RawNandOptions& device_options,
                              fbl::Span<const RawNandImageFlag> flags, ImageFormat format,
                              Writer* writer) {
  if (writer == nullptr) {
    return fit::error(
        "Failed to created |FtlRawNandImageWriter|. Argument |writer| must be a non null.");
  }

  if (device_options.oob_bytes_size == 0 || device_options.pages_per_block == 0) {
    return fit::error(
        "Failed to create |FtlRawNandImageWriter|. Arguments |device_options| must have non zero "
        "|oob_bytes_size| and non-zero |pages_per_block|.");
  }

  RawNandOptions ftl_options = device_options;
  uint32_t multiplier = 1;

  // Find the number of pages to coalesce. Needs to be a divisor of the pages per block, since
  // we can only coalesce pages within the same block.
  while (ftl_options.oob_bytes_size < ftl_image_internal::kFtlMinOobByteSize) {
    // Keep multiplier within block_boundaries.
    do {
      multiplier++;
    } while (device_options.pages_per_block % multiplier != 0 &&
             multiplier <= device_options.pages_per_block);

    if (static_cast<uint32_t>(multiplier) > device_options.pages_per_block) {
      return fit::error(
          "FtlRawNandImageWriter failed to create. Not enough spare bytes in block for the FTL.");
    }

    ftl_options.page_size = device_options.page_size * multiplier;
    ftl_options.oob_bytes_size = device_options.oob_bytes_size * multiplier;
    ftl_options.page_count = device_options.page_count / multiplier;
    ftl_options.pages_per_block = device_options.pages_per_block / multiplier;
  }

  RawNandImageHeader header;
  header.format = format;
  header.page_size = device_options.page_size;
  header.oob_size = device_options.oob_bytes_size;
  for (auto flag : flags) {
    header.flags |= static_cast<std::underlying_type<RawNandImageFlag>::type>(flag);
  }

  auto write_result =
      writer->Write(0, fbl::Span<const uint8_t>(reinterpret_cast<const uint8_t*>(&header),
                                                sizeof(RawNandImageHeader)));
  if (write_result.is_error()) {
    return write_result.take_error_result();
  }

  return fit::ok(
      std::make_tuple(FtlRawNandImageWriter(device_options, multiplier, writer), ftl_options));
}

fit::result<void, std::string> FtlRawNandImageWriter::Write(uint64_t offset,
                                                            fbl::Span<const uint8_t> data) {
  uint64_t device_adjusted_page_size = RawNandImageGetAdjustedPageSize(options_);
  uint64_t adjusted_page_size = scale_factor_ * device_adjusted_page_size;
  uint64_t page_offset = offset % adjusted_page_size;
  uint64_t page_number = offset / adjusted_page_size;
  uint64_t physical_page_per_logical = scale_factor_;
  uint64_t base_image_page_offset =
      RawNandImageGetPageOffset(page_number * scale_factor_, options_) + sizeof(RawNandImageHeader);

  // It is a page write.
  if (page_offset == 0) {
    if (data.size() != physical_page_per_logical * options_.page_size) {
      return fit::error(
          "FtlRawNandImageWriter requires buffer size match the number of physical pages per "
          "logical page.");
    }

    // Write each page.
    for (uint64_t physical_page_offset = 0; physical_page_offset < physical_page_per_logical;
         ++physical_page_offset) {
      auto relative_physical_page_offset = physical_page_offset * options_.page_size;
      auto physical_page_view = data.subspan(relative_physical_page_offset, options_.page_size);

      auto image_relative_page_offset = physical_page_offset * device_adjusted_page_size;
      auto write_result =
          writer_->Write(base_image_page_offset + image_relative_page_offset, physical_page_view);
      if (write_result.is_error()) {
        return write_result;
      }
    }
    return fit::ok();
  }

  // Write each oob area.
  if (page_offset == physical_page_per_logical * options_.page_size) {
    if (data.size() != physical_page_per_logical * options_.oob_bytes_size) {
      return fit::error(
          "FtlRawNandImageWriter requires buffer size match the number of physical oob area per "
          "logical oob area per logical page.");
    }

    // Write each page.
    for (uint64_t physical_page_offset = 0; physical_page_offset < physical_page_per_logical;
         ++physical_page_offset) {
      auto relative_physical_page_offset = physical_page_offset * options_.oob_bytes_size;
      auto physical_page_view =
          data.subspan(relative_physical_page_offset, options_.oob_bytes_size);

      auto image_relative_page_oob_offset =
          physical_page_offset * device_adjusted_page_size + options_.page_size;
      auto write_result = writer_->Write(base_image_page_offset + image_relative_page_oob_offset,
                                         physical_page_view);
      if (write_result.is_error()) {
        return write_result;
      }
    }

    return fit::ok();
  }

  return fit::error(
      "FtlRawNandImageWriter write failed. Unaligned page or unaligned oob write. Actual "
      "offset " +
      std::to_string(page_offset));
}

}  // namespace storage::volume_image
