// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_image.h"

#include <lib/fit/result.h>

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include <fbl/span.h>

#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/ftl/ftl_image_internal.h"
#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/ftl/raw_nand_image.h"
#include "src/storage/volume_image/ftl/raw_nand_image_utils.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"

namespace storage::volume_image {
namespace {

// Write volume page as a succession of physical pages, and use the mapping to
// convert the raw FTL image into a sparse block image.
class FtlPageWriter {
 public:
  explicit FtlPageWriter(const RawNandOptions& ftl_options) : options_(ftl_options) {}

  // Returns |fit::ok| on success, when a new |RawNandPage| has been written with |page_content| in
  // the data section and the appropiate FTL metadata in the spare area section for a volume page,
  // into |writer|.
  fit::result<void, std::string> WriteVolumePage(uint64_t logical_page,
                                                 fbl::Span<const uint8_t> page_content,
                                                 Writer* writer) {
    std::vector<uint8_t> oob_byte_buffer(options_.oob_bytes_size, 0xFF);
    ftl_image_internal::WriteOutOfBandBytes<ftl_image_internal::PageType::kVolumePage>(
        logical_page, oob_byte_buffer);
    uint64_t page_offset = RawNandImageGetPageOffset(physical_page_count_, options_);

    auto write_result = RawNandImageWritePage(page_content, oob_byte_buffer, page_offset, writer);
    if (write_result.is_error()) {
      return write_result.take_error_result();
    }
    if (logical_to_physical_map_.find(logical_page) != logical_to_physical_map_.end()) {
      return fit::error("FTL Image: |Partition::address().mappings| may not share pages.");
    }

    logical_to_physical_map_[logical_page] = physical_page_count_;
    physical_page_count_++;
    return fit::ok();
  }

  // Returns |fit::ok| on success, writing all map pages required to support the written volume
  // pages, in the next available block, since the FTL does not share blocks between volume and map
  // pages.
  fit::result<void, std::string> WriteMapBlock(Writer* writer) {
    uint64_t next_free_page_offset = RawNandImageGetPageOffset(physical_page_count_, options_);
    uint64_t start_of_block_offset =
        RawNandImageGetNextEraseBlockOffset(next_free_page_offset, options_);
    auto result = ftl_image_internal::WriteMapBlock(logical_to_physical_map_, options_,
                                                    start_of_block_offset, writer);
    return result;
  }

 private:
  const RawNandOptions& options_;

  uint64_t physical_page_count_ = 0;

  std::map<uint32_t, uint32_t> logical_to_physical_map_;
};

}  // namespace

fit::result<void, std::string> FtlImageWrite(const RawNandOptions& options,
                                             const Partition& partition, Writer* writer) {
  if (options.oob_bytes_size < ftl_image_internal::kFtlMinOobByteSize) {
    return fit::error("FTL requires at least " +
                      std::to_string(ftl_image_internal::kFtlMinOobByteSize) +
                      " bytes in OOB bytes. Requested OOB bytes size is " +
                      std::to_string(options.oob_bytes_size) + ".");
  }

  FtlPageWriter ftl_writer(options);
  std::vector<uint8_t> page_buffer(options.page_size);

  for (const auto& mapping : partition.address().mappings) {
    uint64_t byte_count = mapping.size.value_or(mapping.count);
    uint64_t logical_page_start = GetBlockFromBytes(mapping.target, options.page_size);
    uint64_t written_page_count = GetBlockCount(mapping.target, mapping.count, options.page_size);
    uint64_t zeroed_page_count =
        GetBlockCount(mapping.target, byte_count, options.page_size) - written_page_count;
    uint64_t read_bytes = 0;

    // Read from the source reader the bytes that go in each page backed by the partition reader.
    for (uint32_t i = 0; i < written_page_count; ++i) {
      page_buffer.assign(page_buffer.size(), 0);
      uint64_t read_offset = mapping.source + read_bytes;
      uint64_t current_offset = mapping.target + read_bytes;
      uint64_t current_page_start = GetOffsetFromBlockStart(current_offset, options.page_size);
      uint64_t remaining_bytes = mapping.count - read_bytes;

      uint64_t buffer_size = remaining_bytes;
      if (current_page_start + remaining_bytes > options.page_size) {
        buffer_size = options.page_size - current_page_start;
      }

      auto view = fbl::Span<uint8_t>(page_buffer).subspan(current_page_start, buffer_size);

      auto read_result = partition.reader()->Read(read_offset, view);
      if (read_result.is_error()) {
        return read_result.take_error_result();
      }
      read_bytes += view.size();

      auto write_result = ftl_writer.WriteVolumePage(logical_page_start + i, page_buffer, writer);
      if (write_result.is_error()) {
        return write_result.take_error_result();
      }
    }

    // We should only write and map this pages if we need to fill with some content, otherwise,
    // the FTL will either return garbage when read or will map a page on demand when written to.
    if (mapping.options.find(EnumAsString(AddressMapOption::kFill)) == mapping.options.end()) {
      continue;
    }

    // Clear the page buffer.
    page_buffer.assign(page_buffer.size(), 0);
    for (uint32_t i = 0; i < zeroed_page_count; ++i) {
      auto write_result = ftl_writer.WriteVolumePage(logical_page_start + written_page_count + i,
                                                     page_buffer, writer);
      if (write_result.is_error()) {
        return write_result.take_error_result();
      }
    }
  }

  return ftl_writer.WriteMapBlock(writer);
}

}  // namespace storage::volume_image
