// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/fvm/fvm_metadata.h"

#include <lib/stdcompat/span.h>

#include <memory>
#include <vector>

#include "src/storage/fvm/metadata.h"
#include "src/storage/fvm/metadata_buffer.h"
#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {

fpromise::result<fvm::Metadata, std::string> FvmGetMetadata(const Reader& source_image) {
  fvm::Header header = {};
  auto header_view = cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&header), sizeof(fvm::Header));

  if (auto header_read_result = source_image.Read(0, header_view); header_read_result.is_error()) {
    return header_read_result.take_error_result();
  }

  if (header.magic != fvm::kMagic) {
    return fpromise::error(
        "|source_image| must be a valid FVM block image. FVM Magic mismatch, found " +
        std::to_string(header.magic) + " expected " + std::to_string(fvm::kMagic));
  }

  auto primary_metadata_buffer = std::make_unique<uint8_t[]>(header.GetMetadataAllocatedBytes());
  if (auto primary_metadata_read_result = source_image.Read(
          header.GetSuperblockOffset(fvm::SuperblockType::kPrimary),
          cpp20::span<uint8_t>(primary_metadata_buffer.get(), header.GetMetadataAllocatedBytes()));
      primary_metadata_read_result.is_error()) {
    return primary_metadata_read_result.take_error_result();
  }
  auto primary_metadata = std::make_unique<fvm::HeapMetadataBuffer>(
      std::move(primary_metadata_buffer), header.GetMetadataAllocatedBytes());

  auto secondary_metadata_buffer = std::make_unique<uint8_t[]>(header.GetMetadataAllocatedBytes());
  if (auto secondary_metadata_read_result =
          source_image.Read(header.GetSuperblockOffset(fvm::SuperblockType::kSecondary),
                            cpp20::span<uint8_t>(secondary_metadata_buffer.get(),
                                                 header.GetMetadataAllocatedBytes()));
      secondary_metadata_read_result.is_error()) {
    return secondary_metadata_read_result.take_error_result();
  }
  auto secondary_metadata = std::make_unique<fvm::HeapMetadataBuffer>(
      std::move(secondary_metadata_buffer), header.GetMetadataAllocatedBytes());

  auto metadata = fvm::Metadata::Create(std::move(primary_metadata), std::move(secondary_metadata));
  if (metadata.is_error()) {
    return fpromise::error("Failed to create FVM Metadata from image. Error Code: " +
                           std::to_string(metadata.error_value()));
  }
  return fpromise::ok(std::move(metadata.value()));
}

}  // namespace storage::volume_image
