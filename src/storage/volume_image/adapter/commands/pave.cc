// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fpromise/result.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/volume_image/adapter/commands.h"
#include "src/storage/volume_image/adapter/mtd_writer.h"
#include "src/storage/volume_image/ftl/ftl_io.h"
#include "src/storage/volume_image/fvm/fvm_descriptor.h"
#include "src/storage/volume_image/fvm/fvm_sparse_image.h"
#include "src/storage/volume_image/utils/block_writer.h"
#include "src/storage/volume_image/utils/bounded_writer.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/fd_writer.h"

namespace storage::volume_image {
namespace {

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

fpromise::result<uint64_t, std::string> GetSize(std::string_view path) {
  std::string str_path(path);
  fbl::unique_fd device(open(str_path.c_str(), O_RDONLY));

  if (!device.is_valid()) {
    auto err = std::string(strerror(errno));
    return fpromise::error("Failed to obtain FD for device at " + str_path +
                           ". More specifically: " + err + ".");
  }

  off_t ret = lseek(device.get(), 0, SEEK_END);
  if (ret < 0) {
    auto err = std::string(strerror(errno));
    return fpromise::error("Failed to seek to end of stream at " + str_path +
                           ". More specifically: " + err + ".");
  }

  return fpromise::ok(static_cast<uint64_t>(ret));
}

}  // namespace

fpromise::result<void, std::string> Pave(const PaveParams& params) {
  if (params.output_path.empty()) {
    return fpromise::error("No image output path provided for Pave.");
  }

  if (params.input_path.empty()) {
    return fpromise::error(
        "No image input path provided for Pave. Must provide path to sparse fvm image.");
  }

  if (params.is_output_embedded) {
    if (!params.offset.has_value()) {
      return fpromise::error("Must provide offset for embedding fvm image.");
    }

    // For block devices we use remaining space.
    if (!params.length.has_value() && params.type == TargetType::kFile) {
      return fpromise::error("Must provide length for embedding fvm image.");
    }
  }

  std::unique_ptr<Writer> writer = nullptr;
  // Depending on target device we may use a different default value.
  std::optional<uint64_t> default_target_length;

  switch (params.type) {
    case TargetType::kBlockDevice: {
      auto writer_or = FdWriter::Create(params.output_path);
      if (writer_or.is_error()) {
        return writer_or.take_error_result();
      }
      std::unique_ptr<FdWriter> fd_writer = std::make_unique<FdWriter>(writer_or.take_value());

      auto reader_or = FdReader::Create(params.output_path);
      if (reader_or.is_error()) {
        return reader_or.take_error_result();
      }
      std::unique_ptr<FdReader> fd_reader = std::make_unique<FdReader>(reader_or.take_value());
      auto block_info_or = GetBlockInfo(params.output_path);
      if (block_info_or.is_error()) {
        return block_info_or.take_error_result();
      }

      if (params.offset.has_value() &&
          params.offset.value() % block_info_or.value().st_blksize != 0) {
        return fpromise::error(
            "Offset must be aligned to block boundary for paving a block device.");
      }

      auto size_or = GetSize(params.output_path);
      if (size_or.is_error()) {
        return size_or.take_error_result();
      }
      default_target_length = size_or.take_value() - params.offset.value_or(0);
      uint64_t block_count =
          params.length.value_or(default_target_length.value()) / block_info_or.value().st_blksize;
      writer = std::make_unique<BlockWriter>(block_info_or.value().st_blksize, block_count,
                                             std::move(fd_reader), std::move(fd_writer));
      break;
    }

    case TargetType::kMtd: {
      MtdParams mtd_params;
      mtd_params.offset = params.offset.value_or(0);
      mtd_params.format = true;
      if (!params.max_bad_blocks.has_value()) {
        return fpromise::error("Pave to |kMtd| target, requires |max_bad_blocks| to be set.");
      }
      mtd_params.max_bad_blocks = params.max_bad_blocks.value();
      FtlHandle handle;
      auto mtd_writer_or = CreateMtdWriter(params.output_path, mtd_params, &handle);
      if (mtd_writer_or.is_error()) {
        return mtd_writer_or.take_error_result();
      }
      default_target_length = handle.instance().page_count() * handle.instance().page_size();
      writer = mtd_writer_or.take_value();
      break;
    }

    case TargetType::kFile: {
      auto writer_or = FdWriter::Create(params.output_path);
      if (writer_or.is_error()) {
        return writer_or.take_error_result();
      }
      auto size_or = GetSize(params.output_path);
      if (size_or.is_error()) {
        return size_or.take_error_result();
      }
      default_target_length = size_or.value() - params.offset.value_or(0);
      writer = std::make_unique<FdWriter>(writer_or.take_value());
      break;
    }
  };

  auto reader_or = FdReader::Create(params.input_path);
  if (reader_or.is_error()) {
    return reader_or.take_error_result();
  }
  std::unique_ptr<Reader> reader = std::make_unique<FdReader>(reader_or.take_value());
  auto length = params.length.value_or(default_target_length.value());

  if (params.is_output_embedded) {
    // The MTD Writer handles the offset internally.
    writer = std::make_unique<BoundedWriter>(
        std::move(writer), (params.type == TargetType::kMtd) ? 0 : params.offset.value(), length);
  }

  auto descriptor_or = FvmSparseReadImage(0, std::move(reader));
  if (descriptor_or.is_error()) {
    return descriptor_or.take_error_result();
  }

  // So we can update the options.
  auto updated_options = descriptor_or.value().options();

  updated_options.target_volume_size = length;
  if (updated_options.max_volume_size.value_or(0) <
      params.fvm_options.max_volume_size.value_or(0)) {
    updated_options.max_volume_size = params.fvm_options.max_volume_size;
  }
  updated_options.compression = {.schema = CompressionSchema::kNone};

  auto updated_descriptor_or =
      FvmDescriptor::Builder(descriptor_or.take_value()).SetOptions(updated_options).Build();
  if (updated_descriptor_or.is_error()) {
    return updated_descriptor_or.take_error_result();
  }

  auto pave_result = updated_descriptor_or.value().WriteBlockImage(*writer);
  if (pave_result.is_error()) {
    return pave_result.take_error_result();
  }

  return fpromise::ok();
}

}  // namespace storage::volume_image
