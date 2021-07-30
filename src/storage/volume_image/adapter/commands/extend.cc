// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fit/result.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/fvm/fvm_image_extend.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_writer.h"

namespace storage::volume_image {
namespace {

// A helper wrapping fstat.
fpromise::result<struct stat, std::string> GetBlockInfo(std::string_view path) {
  std::string str_path(path);
  fbl::unique_fd device(open(str_path.c_str(), O_RDONLY));

  if (!device.is_valid()) {
    auto err = std::string(strerror(errno));
    return fpromise::error("Failed to obtain FD for device at " + str_path +
                           ". More specifically: " + err + ".");
  }

  struct stat st = {};
  if (fstat(device.get(), &st) != 0) {
    auto err = std::string(strerror(errno));
    return fpromise::error("Failed to perform fstat on device. More specifically: " + err + ".");
  }

  return fpromise::ok(st);
}

}  // namespace

fit::result<void, std::string> Extend(const ExtendParams& params) {
  if (params.image_path.empty()) {
    return fit::error("Must provide a non empty |image_path| for extend.");
  }

  auto block_info_or = GetBlockInfo(params.image_path);
  if (block_info_or.is_error()) {
    return block_info_or.take_error_result();
  }

  if (params.length.value() < static_cast<uint64_t>(block_info_or.value().st_size)) {
    return fit::error("|length|(" + std::to_string(params.length.value()) +
                      ") must be greater or equal than |disk_size|(" +
                      std::to_string(block_info_or.value().st_size) + " bytes)");
  }

  auto reader_or = FdReader::Create(params.image_path);
  if (reader_or.is_error()) {
    return reader_or.take_error_result();
  }
  auto image_reader = reader_or.take_value();

  auto image_size_or = FvmImageGetSize(image_reader);
  if (image_size_or.is_error()) {
    return image_size_or.take_error_result();
  }
  uint64_t image_size = image_size_or.value();

  uint64_t target_volume_size = params.length.value();
  if (params.should_use_max_partition_size && target_volume_size < image_size) {
    target_volume_size = image_size;
  }
  FvmOptions options;
  options.target_volume_size = target_volume_size;

  // We create a temporary file to protect source image from IO errors.
  std::string base =
      std::filesystem::path(params.image_path).parent_path().string() + "/tmp_XXXXXXX";

  fbl::unique_fd tmp_file(mkstemp(base.data()));
  if (!tmp_file.is_valid()) {
    return fpromise::error("Failed to create temporary file at " + base +
                           ". More specifically: " + strerror(errno));
  }

  {
    // So writer is flushed.
    FdWriter writer(std::move(tmp_file));

    auto extend_or = FvmImageExtend(image_reader, options, writer);
    if (extend_or.is_error()) {
      return extend_or.take_error_result();
    }
  }

  uint64_t truncate_size = target_volume_size;
  if (params.should_trim) {
    auto trim_size_or = FvmImageGetTrimmedSize(image_reader);
    if (trim_size_or.is_error()) {
      return trim_size_or.take_error_result();
    }
    truncate_size = trim_size_or.value();
  }

  // Now truncate the image to target size. The target size is either the partition size,
  // the image is ready to me used or its trim size, that is the image has no trailing unallocated
  // slices.
  if (truncate(base.data(), static_cast<off_t>(truncate_size)) != 0) {
    std::string err(strerror(errno));
    return fit::error("Failed to trim image to " + std::to_string(truncate_size) +
                      ". More specifically " + err + ".");
  }

  if (rename(base.data(), params.image_path.c_str()) != 0) {
    std::string err(strerror(errno));
    return fit::error("Failed to move temporary image(working copy at " + base +
                      ") to final location(source image at " + params.image_path +
                      ". More specifically: " + err + ".");
  }

  return fit::ok();
}

}  // namespace storage::volume_image
