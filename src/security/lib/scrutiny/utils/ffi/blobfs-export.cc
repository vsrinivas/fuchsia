/*
 * Copyright 2022 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/security/lib/scrutiny/utils/ffi/blobfs-export.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/errors.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <fbl/unique_fd.h>

#include "src/storage/blobfs/host.h"

int blobfs_export_blobs(const char* source_path, const char* output_path) {
  fbl::unique_fd blobfs_image(open(source_path, O_RDONLY));
  if (!blobfs_image.is_valid()) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  std::unique_ptr<blobfs::Blobfs> fs = nullptr;
  zx_status_t status = blobfs::blobfs_create(&fs, std::move(blobfs_image));
  if (status != ZX_OK) {
    return status;
  }

  std::filesystem::create_directories(output_path);
  fbl::unique_fd output_fd(open(output_path, O_DIRECTORY));
  if (!output_fd.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }
  auto export_result = blobfs::ExportBlobs(output_fd.get(), *fs);
  if (export_result.is_error()) {
    return -1;
  }
  return ZX_OK;
}
