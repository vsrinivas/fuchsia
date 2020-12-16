// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/bin/fvm/mtd.h"

#include <lib/ftl-mtd/ftl-volume-wrapper.h>
#include <lib/ftl-mtd/nand-volume-driver.h>
#include <lib/mtd/mtd-interface.h>
#include <unistd.h>
#include <zircon/types.h>

#include <memory>

zx_status_t CreateFileWrapperFromMtd(const char* path, uint32_t offset, uint32_t max_bad_blocks,
                                     std::unique_ptr<fvm::host::FileWrapper>* wrapper) {
  std::unique_ptr<mtd::MtdInterface> interface = mtd::MtdInterface::Create(path);
  if (!interface) {
    fprintf(stderr, "Failed to create MTD interface at %s\n", path);
    return ZX_ERR_IO;
  }

  std::unique_ptr<ftl_mtd::NandVolumeDriver> driver;
  uint32_t block_size = interface->BlockSize();

  if (offset % block_size != 0) {
    fprintf(stderr, "Offset must be divisble by MTD block size of %u\n", block_size);
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t block_offset = offset / block_size;

  zx_status_t status = ftl_mtd::NandVolumeDriver::Create(block_offset, max_bad_blocks,
                                                         std::move(interface), &driver);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to create NandVolumeDriver\n");
    return status;
  }

  const char* error = driver->Init();
  if (error) {
    fprintf(stderr, "Failed to initialize NandVolumeDriver: %s\n", error);
    return ZX_ERR_BAD_STATE;
  }

  auto ftl_wrapper = std::make_unique<ftl_mtd::FtlVolumeWrapper>();

  status = ftl_wrapper->Init(std::move(driver));
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to initialize FTL volume\n");
    return status;
  }

  status = ftl_wrapper->Format();
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to format FTL volume\n");
    return status;
  }

  *wrapper = std::move(ftl_wrapper);
  return ZX_OK;
}
