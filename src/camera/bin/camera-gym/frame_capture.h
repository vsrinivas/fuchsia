// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_FRAME_CAPTURE_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_FRAME_CAPTURE_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include <mutex>
#include <queue>

namespace camera {

class CapturedFrame {
 public:
  CapturedFrame(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size, std::string path_name)
      : buffer_(std::move(buffer)), buffer_size_(buffer_size), path_name_(std::move(path_name)) {}
  ~CapturedFrame() = default;

  std::unique_ptr<uint8_t[]> take_buffer() { return std::move(buffer_); }
  uint32_t buffer_size() const { return buffer_size_; }
  std::string take_path_name() { return std::move(path_name_); }

 private:
  std::unique_ptr<uint8_t[]> buffer_;
  uint32_t buffer_size_;
  std::string path_name_;
};

class FrameCapture {
 public:
  FrameCapture();
  ~FrameCapture() = default;

  // Must initialize before attempting to capture.
  zx_status_t Initialize();

  // Caller must call each time with the VMO of interest, along with the image width, height, stride
  // and image size. The image format is assumed to be NV12.
  void Capture(const zx::vmo& vmo, uint32_t coded_width, uint32_t coded_height,
               uint32_t coded_stride, uint32_t coded_image_size);

 private:
  async::Loop archive_loop_;

  // Do the actual work of archiving the captured frame by writing to the file system.
  void Archive(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size, std::string path_name);

  // Dispatch work whenever work is available.
  void ArchiveTask();

  // Set of captured frames.
  // "captured_frame_count" goes from 0 to N-1 (whenever a captured frame is passed in).
  // "archived_frame_count" goes from 0 to N-1 (whenever a frame is about to be written to a file).
  // This is really just an explicit queue with a handshake to ensure previous archive operation
  // is done.
  uint32_t captured_frame_count_ = 0;
  uint32_t archived_frame_count_ = 0;
  std::mutex captured_frames_lock_;
  std::queue<std::unique_ptr<CapturedFrame>> captured_frames_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_FRAME_CAPTURE_H_
