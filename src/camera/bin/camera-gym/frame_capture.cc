// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/camera-gym/frame_capture.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <fbl/unique_fd.h>

namespace camera {

const char* base_dir = "/tmp";

FrameCapture::FrameCapture() : archive_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

zx_status_t FrameCapture::Initialize() {
  zx_status_t status = archive_loop_.StartThread("Archive Thread");
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create Archive Thread";
    return status;
  }
  return ZX_OK;
}

void FrameCapture::Capture(const zx::vmo& vmo, uint32_t coded_width, uint32_t coded_height,
                           uint32_t coded_stride, uint32_t coded_image_size) {
  ZX_ASSERT(coded_width > 0);
  ZX_ASSERT(coded_height > 0);
  ZX_ASSERT(coded_stride > 0);
  ZX_ASSERT(coded_image_size >= 4096);

  // Make a copy of the frame buffer.
  auto buffer = std::make_unique<uint8_t[]>(coded_image_size);
  uint64_t offset = 0;
  zx_status_t status =
      vmo.op_range(ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, offset, coded_image_size, nullptr, 0);
  ZX_ASSERT(status == ZX_OK);
  status = vmo.read(buffer.get(), offset, coded_image_size);
  ZX_ASSERT(status == ZX_OK);

  // Construct base file name.
  std::stringstream base_name;
  base_name << base_dir << "/image_" << coded_width << "x" << coded_height << "_"
            << captured_frame_count_ << ".nv12";

  auto captured_frame =
      std::make_unique<CapturedFrame>(std::move(buffer), coded_image_size, base_name.str());

  {
    std::lock_guard<std::mutex> lock(captured_frames_lock_);
    captured_frames_.push(std::move(captured_frame));
    ++captured_frame_count_;
  }

  async::PostTask(archive_loop_.dispatcher(), [this]() { ArchiveTask(); });
}

void FrameCapture::Archive(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size,
                           std::string path_name) {
  int fd = open(path_name.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    FX_LOGS(ERROR) << "Error opening file at " << path_name.c_str() << ": " << strerror(errno);
    return;
  }
  int result = static_cast<int>(write(fd, buffer.get(), buffer_size));
  if (result < 0) {
    FX_LOGS(ERROR) << "Error writing file at " << path_name.c_str() << ": " << strerror(errno);
  } else if (result != static_cast<int>(buffer_size)) {
    FX_LOGS(ERROR) << "Error writing file at " << path_name.c_str() << ": result != buffer_size";
  }
  close(fd);
}

void FrameCapture::ArchiveTask() {
  while (captured_frame_count_ > archived_frame_count_) {
    std::unique_ptr<CapturedFrame> captured_frame;

    {
      std::lock_guard<std::mutex> lock(captured_frames_lock_);
      captured_frame = std::move(captured_frames_.front());
      captured_frames_.pop();
      ++archived_frame_count_;
    }

    Archive(captured_frame->take_buffer(), captured_frame->buffer_size(),
            captured_frame->take_path_name());
  }
}

}  // namespace camera
