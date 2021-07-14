// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ftl-mtd/nand-volume-driver.h>
#include <lib/ftl/volume.h>
#include <lib/mtd/mtd-interface.h>
#include <zircon/errors.h>

#include <cstdint>
#include <memory>

#include <safemath/safe_conversions.h>

#include "src/storage/volume_image/adapter/mtd_writer.h"
#include "src/storage/volume_image/ftl/ftl_io.h"
#include "src/storage/volume_image/utils/block_writer.h"
#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {

fpromise::result<std::unique_ptr<Writer>, std::string> CreateMtdWriter(std::string_view path,
                                                                       const MtdParams& params,
                                                                       FtlHandle* ftl_handle) {
  std::unique_ptr<mtd::MtdInterface> interface = mtd::MtdInterface::Create(std::string(path));
  if (!interface) {
    return fpromise::error("Failed to create MTD interface at " + std::string(path));
  }

  std::unique_ptr<ftl_mtd::NandVolumeDriver> driver;
  uint32_t block_size = interface->BlockSize();

  if (params.offset % block_size != 0) {
    return fpromise::error("MTD Device offset must be NAND Page aligned. Page size is " +
                           std::to_string(block_size) + " and provided offset is " +
                           std::to_string(params.offset) + ".");
  }

  uint32_t block_offset = safemath::checked_cast<uint32_t>(params.offset / block_size);
  uint32_t max_bad_blocks = safemath::checked_cast<uint32_t>(params.max_bad_blocks);

  zx_status_t status = ftl_mtd::NandVolumeDriver::Create(block_offset, max_bad_blocks,
                                                         std::move(interface), &driver);
  if (status != ZX_OK) {
    return fpromise::error(
        "ftl_mtd::NandVolumeDriver creation failed. Error Code: " + std::to_string(status) + ".");
  }

  const char* error = driver->Init();
  if (error != nullptr) {
    return fpromise::error(
        "ftl_mtd::NandVolumeDriver initialization failed failed. More specifically: " +
        std::string(error) + ".");
  }

  std::unique_ptr<FtlHandle> handle = std::make_unique<FtlHandle>();
  if (auto result = handle->Init(std::move(driver)); result.is_error()) {
    return result.take_error_result();
  }
  if (ftl_handle != nullptr) {
    *ftl_handle = *handle;
  }

  if (params.format) {
    status = handle->volume().Format();
    if (status != ZX_OK) {
      return fpromise::error("Device FTL formatting failed. Error code: " + std::to_string(status) +
                             ".");
    }
  }

  return fpromise::ok(std::make_unique<BlockWriter>(handle->instance().page_size(),
                                                    handle->instance().page_count(),
                                                    handle->MakeReader(), handle->MakeWriter()));
}

}  // namespace storage::volume_image
