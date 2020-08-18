// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IMAGE_INTERNAL_H_
#define SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IMAGE_INTERNAL_H_

#include <lib/fit/result.h>

#include <map>
#include <string>

#include <fbl/span.h>

#include "src/storage/volume_image/ftl/options.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image::ftl_image_internal {

// The wear count to be set for initializing a page..
constexpr uint32_t kFtlPageWearCount = 0;

// Mark expected by the NDM layer for non control block pages.
constexpr uint8_t kNdmVolumePageMark = 7;

// Minimum number of OOB bytes required for FTL page metadata.
constexpr int kFtlMinOobByteSize = 16;

// Supported type of pages by the FTL.
enum PageType {
  // Contains volume data as is.
  kVolumePage,

  // Contains mappings from a logical page to a physical page.
  // This pages are in separate blocks.
  kMapPage,
};

// Fills |oob_bytes| with the expected FTL data for a Volume Page.
template <PageType page_type>
void WriteOutOfBandBytes(uint32_t logical_page_number, fbl::Span<uint8_t> oob_bytes);

// Writes a map block into |writer| with the providing |logical_to_physical_pages| mappings,
// assuming the next block starts at |offset|.
fit::result<void, std::string> WriteMapBlock(
    const std::map<uint32_t, uint32_t>& logical_to_physical_pages,
    const RawNandOptions& ftl_options, uint64_t offset, Writer* writer);

}  // namespace storage::volume_image::ftl_image_internal

#endif  // SRC_STORAGE_VOLUME_IMAGE_FTL_FTL_IMAGE_INTERNAL_H_
