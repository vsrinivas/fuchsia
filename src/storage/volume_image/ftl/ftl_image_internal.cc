// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/ftl/ftl_image_internal.h"

#include <iostream>
#include <limits>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/span.h>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/ftl/raw_nand_image_utils.h"

namespace storage::volume_image::ftl_image_internal {
namespace {
// Writes |value| into |sink|, in little endian, as expected by the FTL.
template <typename T, typename std::enable_if<std::is_integral<T>::value, bool>::type = true>
void WriteValue(const T value, fbl::Span<uint8_t> sink) {
  for (size_t i = 0; i < sink.size(); ++i) {
    uint8_t byte = (value >> (i * 8)) & 0xFF;
    sink[i] = byte;
  }
}
// Fills |oob_bytes| with the expected FTL data.
void FillOutOfBandBytes(uint32_t logical_page_number, uint32_t generation_number,
                        fbl::Span<uint8_t> oob_bytes) {
  // Reset the contents of |oob| to unprogrammed state.
  std::fill(oob_bytes.begin(), oob_bytes.end(), 0xFF);

  // Mark the block as not bad. This only matters for the first page in the block,
  // but is innocuous in the other pages.
  // Note: Here for clarification.
  oob_bytes[0] = 0xFF;

  // Fill the logical page number.
  WriteValue(logical_page_number, oob_bytes.subspan(1, 4));
  WriteValue(generation_number, oob_bytes.subspan(5, 4));

  // Write the wear count, that has a length of 28 bits.
  // We write the first 3 bytes as is, then write the following most significant 4 bits in the 12-th
  // byte.
  WriteValue(kFtlPageWearCount, oob_bytes.subspan(9, 3));
  oob_bytes[12] = (oob_bytes[12] & 0xF) | ((kFtlPageWearCount >> 20) & 0xF0);

  // Add the NDM marking this page as a valid volume page.
  oob_bytes[15] = kNdmVolumePageMark;
}
}  // namespace

template <>
void WriteOutOfBandBytes<PageType::kVolumePage>(uint32_t logical_page_number,
                                                fbl::Span<uint8_t> oob_bytes) {
  // Volume pages have the generation number ''unprogrammed'' where all bits are set.
  FillOutOfBandBytes(logical_page_number, std::numeric_limits<uint32_t>::max(), oob_bytes);
}

template <>
void WriteOutOfBandBytes<PageType::kMapPage>(uint32_t logical_page_number,
                                             fbl::Span<uint8_t> oob_bytes) {
  // Generated images have the first version of the map pages, with generation number 0.
  FillOutOfBandBytes(logical_page_number, 0, oob_bytes);
}

fit::result<void, std::string> WriteMapBlock(
    const std::map<uint32_t, uint32_t>& logical_to_physical_pages,
    const RawNandOptions& ftl_options, uint64_t offset, Writer* writer) {
  if (ftl_options.page_size < 4) {
    return fit::error("Page Size must be greater than 4 bytes.");
  }

  if (ftl_options.oob_bytes_size < 16) {
    return fit::error("OOB Size must be greater or equal to 16 bytes per page.");
  }

  uint32_t mappings_per_page = ftl_options.page_size / sizeof(uint32_t);
  uint32_t total_map_pages =
      fbl::round_up(ftl_options.page_count, mappings_per_page) / mappings_per_page;

  std::vector<uint8_t> page_buffer(ftl_options.page_size, 0xFF);
  std::vector<uint8_t> oob_bytes_buffer(ftl_options.oob_bytes_size, 0xFF);

  uint64_t written_pages = 0;
  for (uint32_t map_page_number = 0; map_page_number < total_map_pages; ++map_page_number) {
    std::fill(page_buffer.begin(), page_buffer.end(), 0xFF);
    uint32_t lower_bound = map_page_number * mappings_per_page;
    uint32_t upper_bound = lower_bound + mappings_per_page - 1;
    auto begin = logical_to_physical_pages.lower_bound(lower_bound);
    auto end = logical_to_physical_pages.upper_bound(upper_bound);

    // Fill mappings in this page range.
    for (auto it = begin; it != end; ++it) {
      auto [logical_page, physical_page] = *it;
      size_t mapping_page_offset = logical_page - lower_bound;
      size_t map_page_offset = (mapping_page_offset % mappings_per_page) * sizeof(uint32_t);
      WriteValue(physical_page,
                 fbl::Span<uint8_t>(page_buffer.data() + map_page_offset, sizeof(uint32_t)));
    }

    // Only write map pages that have mappings.
    if (begin == end) {
      continue;
    }

    WriteOutOfBandBytes<PageType::kMapPage>(map_page_number, oob_bytes_buffer);
    auto write_result = RawNandImageWritePage(
        page_buffer, oob_bytes_buffer,
        offset + RawNandImageGetPageOffset(written_pages, ftl_options), writer);
    if (write_result.is_error()) {
      return write_result.take_error_result();
    }
    written_pages++;
  }

  return fit::ok();
}

}  // namespace storage::volume_image::ftl_image_internal
