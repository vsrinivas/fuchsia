// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/result.h>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include <fbl/unique_fd.h>

#include "lib/fpromise/result.h"
#include "src/storage/fvm/format.h"
#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/fvm/fvm_image_extend.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/fd_reader.h"

namespace storage::volume_image {

fpromise::result<uint64_t, std::string> Size(const SizeParams& params) {
  auto image_reader_or = FdReader::Create(params.image_path);
  if (image_reader_or.is_error()) {
    return image_reader_or.take_error_result();
  }

  auto fvm_descriptor_or =
      FvmSparseReadImage(0, std::make_unique<FdReader>(image_reader_or.take_value()));
  if (fvm_descriptor_or.is_error()) {
    return fvm_descriptor_or.take_error_result();
  }
  auto fvm_descriptor = fvm_descriptor_or.take_value();

  if (params.length.has_value()) {
    // Calculate slices that this length can have.
    auto header = fvm::Header::FromDiskSize(fvm::kMaxUsablePartitions, params.length.value(),
                                            fvm_descriptor.options().slice_size);
    if (header.pslice_count < fvm_descriptor.slice_count()) {
      return fpromise::error("Image requires " + std::to_string(fvm_descriptor.slice_count()) +
                             " slices, while target length(" +
                             std::to_string(params.length.value()) + ") can fit only " +
                             std::to_string(header.pslice_count) + " slices.");
    }
  }
  auto header = fvm::Header::FromSliceCount(fvm::kMaxUsablePartitions, fvm_descriptor.slice_count(),
                                            fvm_descriptor.options().slice_size);
  return fpromise::ok(header.fvm_partition_size);
}

}  // namespace storage::volume_image
