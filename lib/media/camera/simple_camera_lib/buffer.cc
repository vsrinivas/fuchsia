// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/lib/media/camera/simple_camera_lib/buffer.h>

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <stdio.h>
#include <unistd.h>

#include <utility>

#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

namespace simple_camera {

zx_status_t Buffer::DuplicateAndMapVmo(uint64_t buffer_size,
                                       const zx::vmo& main_buffer,
                                       uint64_t offset) {
  zx::vmo vmo;
  zx_status_t status = main_buffer.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate vmo (status: " << status << ").";
    return ZX_ERR_INTERNAL;
  }

  status = Map(vmo, offset, buffer_size,
               ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE);

  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Can't map vmo.";
    return status;
  }

  vmo_ = std::move(vmo);
  vmo_offset_ = offset;

  return ZX_OK;
}

std::unique_ptr<Buffer> Buffer::Create(uint64_t buffer_size,
                                       const zx::vmo& main_buffer,
                                       uint64_t offset) {
  std::unique_ptr<Buffer> b(new Buffer);
  zx_status_t status = b->DuplicateAndMapVmo(buffer_size, main_buffer, offset);
  if (status != ZX_OK) {
    return nullptr;
  }
  return b;
}

zx_status_t Buffer::DuplicateVmoWithoutWrite(zx::vmo* result) {
  zx_info_handle_basic_t info;
  zx_status_t status = zx_object_get_info(vmo_.get(), ZX_INFO_HANDLE_BASIC,
                                          &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting basic handle info";
    return status;
  }
  status = vmo_.duplicate(info.rights & ~ZX_RIGHT_WRITE, result);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Error duplicating VMO";
    return status;
  }
  return ZX_OK;
}

void Buffer::FillARGB(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t color = 0xff << 24 | r << 16 | g << 8 | b;
  uint32_t num_pixels = size() / 4;
  uint32_t* pixels = reinterpret_cast<uint32_t*>(start());
  for (unsigned int i = 0; i < num_pixels; i++) {
    pixels[i] = color;
  }

  // Ignore if flushing the cache fails.
  zx_cache_flush(start(), size(),
                 ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
}

zx_status_t Buffer::SaveToFile(const char* filename) {
  int fd = ::open(filename, O_RDWR | O_CREAT);
  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open \"" << filename << "\" err: " << fd;
    return fd;
  }
  write(fd, start(), size());
  close(fd);
  return ZX_OK;
}

}  // namespace simple_camera
