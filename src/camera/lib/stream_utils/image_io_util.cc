// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "image_io_util.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>

#include <vector>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace camera {

fpromise::result<std::unique_ptr<ImageIOUtil>, zx_status_t> ImageIOUtil::Create(
    fuchsia::sysmem::BufferCollectionInfo_2* buffer_collection, const std::string& dir_path) {
  if (buffer_collection->buffer_count <= 0) {
    FX_LOGS(DEBUG) << "Failed to create ImageIOUtil with empty BufferCollection.";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  if (dir_path.empty()) {
    FX_LOGS(DEBUG) << "Failed to create ImageIOUtil with empty dir_path.";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_clone;
  zx_status_t status = buffer_collection->Clone(&buffer_collection_clone);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to clone BufferCollection.";
    return fpromise::error(status);
  }
  auto stream_io = std::make_unique<ImageIOUtil>(std::move(buffer_collection_clone), dir_path);

  if (!files::CreateDirectory(stream_io->GetDirpath())) {
    FX_LOGS(ERROR) << "Failed to create on-disk directory to write to.";
    return fpromise::error(ZX_ERR_IO);
  }

  return fpromise::ok(std::move(stream_io));
}

// TODO(nzo): add functionality to ensure all previously written images (i.e. before this
// ImageIOUtil was created) are also deleted.
zx_status_t ImageIOUtil::DeleteImageData() {
  bool delete_status = true;
  for (uint32_t i = 0; i < num_image_; ++i) {
    delete_status &= files::DeletePath(GetFilepath(i), false);
  }

  return delete_status ? ZX_OK : ZX_ERR_IO;
}

zx_status_t ImageIOUtil::WriteImageData(uint32_t id) {
  if (id >= buffer_collection_.buffer_count) {
    FX_LOGS(ERROR) << "An invalid buffer id was passed in.";
    return ZX_ERR_INVALID_ARGS;
  }
  // TODO(nzo): change this to use information from ImageFormat_2 instead.
  size_t num_bytes = buffer_collection_.settings.buffer_settings.size_bytes;

  fzl::VmoMapper mapper;
  zx_status_t status =
      mapper.Map(buffer_collection_.buffers[id].vmo, 0, num_bytes, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to map vmo.";
    return status;
  }

  if (!files::WriteFile(GetFilepath(num_image_), static_cast<const char*>(mapper.start()),
                        num_bytes)) {
    FX_LOGS(ERROR) << "Failed to write to file on disk.";
    return ZX_ERR_IO;
  }

  FX_LOGS(INFO) << "Image written to disk.\n Run `fx scp " << GetFilepath(num_image_)
                << " ~/[HOST DESTINATION]` to transfer the file from device.";
  ++num_image_;

  return ZX_OK;
}

}  // namespace camera
