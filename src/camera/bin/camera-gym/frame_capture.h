// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_FRAME_CAPTURE_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_FRAME_CAPTURE_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <mutex>
#include <queue>

namespace camera {

class CapturedFrame {
 public:
  CapturedFrame(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size, std::string base_file_name)
      : buffer_(std::move(buffer)),
        buffer_size_(buffer_size),
        base_file_name_(std::move(base_file_name)) {}
  ~CapturedFrame() = default;

  std::unique_ptr<uint8_t[]> take_buffer() { return std::move(buffer_); }
  uint32_t buffer_size() const { return buffer_size_; }
  std::string take_base_file_name() { return std::move(base_file_name_); }

 private:
  std::unique_ptr<uint8_t[]> buffer_;
  uint32_t buffer_size_;
  std::string base_file_name_;
};

class FrameCapture {
 public:
  // Caller must start two independent async::Loop & pass in the dispatchers.
  FrameCapture() = default;
  ~FrameCapture() = default;

  // Caller must call each time with an newly allocated & filled buffer & its associated file
  // name. Caller shall expect to relinquish ownership of this buffer. Work posted on capture
  // dispatcher, so this should be fire & forget with very little delay to caller.
  void Capture(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size, std::string base_file_name);

  void set_capture_dispatcher(async_dispatcher_t* dispatcher) { capture_dispatcher_ = dispatcher; }
  void set_archive_dispatcher(async_dispatcher_t* dispatcher) { archive_dispatcher_ = dispatcher; }

 private:
  async_dispatcher_t* capture_dispatcher_;
  async_dispatcher_t* archive_dispatcher_;

  // Must do this work on private thread to be safe.
  void PostedCapture(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size,
                     std::string base_file_name);

  void Archive(std::unique_ptr<uint8_t[]> buffer, uint32_t buffer_size, std::string base_file_name);

  void ArchiveTask();

  // Set of captured frames.
  // "captured_frame_count" goes from 0 to N-1.
  // "archived_frame_count" goes from 0 to N-1.
  // This is really just an explicit queue with a handshake to ensure previous archive operation
  // is done.
  uint32_t captured_frame_count_ = 0;
  uint32_t archived_frame_count_ = 0;
  std::queue<std::unique_ptr<CapturedFrame>> captured_frames_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_FRAME_CAPTURE_H_
