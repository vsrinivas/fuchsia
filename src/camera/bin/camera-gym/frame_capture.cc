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

void FrameCapture::Capture(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size,
                           std::string base_file_name) {
  async::PostTask(capture_dispatcher_, [this, buffer = std::move(buffer), buffer_size,
                                        base_file_name = std::move(base_file_name)]() mutable {
    PostedCapture(std::move(buffer), buffer_size, std::move(base_file_name));
  });
}

void FrameCapture::PostedCapture(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size,
                                 std::string base_file_name) {
  auto captured_frame =
      std::make_unique<CapturedFrame>(std::move(buffer), buffer_size, std::move(base_file_name));
  captured_frames_.push(std::move(captured_frame));
  ++captured_frame_count_;
  async::PostTask(archive_dispatcher_, [this]() { ArchiveTask(); });
}

void FrameCapture::Archive(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size,
                           std::string base_file_name) {
  std::stringstream path_name;
  path_name << base_file_name << "_" << archived_frame_count_ << ".nv12";
  ++archived_frame_count_;
  int fd = open(path_name.str().c_str(), O_WRONLY | O_CREAT);
  if (fd < 0) {
    FX_LOGS(ERROR) << "Error opening file at " << path_name.str() << ": " << strerror(errno);
    return;
  }
  int result = write(fd, buffer.get(), buffer_size);
  if (result < 0) {
    FX_LOGS(ERROR) << "Error writing file at " << path_name.str() << ": " << strerror(errno);
  } else if (result != static_cast<int>(buffer_size)) {
    FX_LOGS(ERROR) << "Error writing file at " << path_name.str() << ": result != buffer_size";
  }
  close(fd);
}

void FrameCapture::ArchiveTask() {
  while (captured_frame_count_ > archived_frame_count_) {
    std::unique_ptr<CapturedFrame> captured_frame = std::move(captured_frames_.front());
    captured_frames_.pop();
    Archive(captured_frame->take_buffer(), captured_frame->buffer_size(),
            captured_frame->take_base_file_name());
  }
}

}  // namespace camera
