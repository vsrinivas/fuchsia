// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "in_stream_file.h"

#include "util.h"

namespace {

// Reads <= this size will complete in their entirety.  Reads > this size may
// read less.
const uint64_t kCompleteReadThresholdBytes = 1;
static_assert(kCompleteReadThresholdBytes >= 1);

}  // namespace

InStreamFile::InStreamFile(async::Loop* fidl_loop, thrd_t fidl_thread,
                           sys::ComponentContext* component_context, std::string input_file_name)
    : InStream(fidl_loop, fidl_thread, component_context), input_file_name_(input_file_name) {
  // std::ios::ate means start at the end to tellg() will get the size
  file_.open(input_file_name_.c_str(), std::ios::in | std::ios::binary | std::ios::ate);
  if (!file_.is_open()) {
    Exit("failed to open file %s", input_file_name_.c_str());
  }
  std::streampos input_size = file_.tellg();
  if (input_size == -1) {
    Exit("file.tellg() failed");
  }
  eos_position_ = input_size;
  eos_position_known_ = true;
  file_.seekg(0, std::ios::beg);
  if (!file_) {
    Exit("file_.seekg(0, beg) failed");
  }
  ZX_DEBUG_ASSERT(cursor_position_ == 0);
}

InStreamFile::~InStreamFile() {
  file_.close();
  if (!file_) {
    Exit("file.close() failed");
  }
}

zx_status_t InStreamFile::ReadBytesInternal(uint32_t max_bytes_to_read, uint32_t* bytes_read_out,
                                            uint8_t* buffer_out, zx::time just_fail_deadline) {
  // This sub-class doesn't enforce just_fail_deadline for now.
  (void)just_fail_deadline;
  // This implementation ignores the timeout, as we're reading from a local file
  // so not worth bothering with the timeout at least for now.
  ZX_DEBUG_ASSERT(static_cast<uint64_t>(file_.tellg()) == cursor_position_);
  ZX_DEBUG_ASSERT(eos_position_known_);
  ZX_DEBUG_ASSERT(cursor_position_ <= eos_position_);
  uint64_t bytes_to_read = max_bytes_to_read;
  if (cursor_position_ + bytes_to_read > eos_position_) {
    bytes_to_read = eos_position_ - cursor_position_;
  }
  if (!bytes_to_read) {
    // This indicates EOS.
    *bytes_read_out = 0;
    return ZX_OK;
  }
  // To avoid taking a dependency on complete reads, which in general InStream
  // doesn't guarantee, we intentionally don't read as much as requested
  // sometimes.  Yes this does force the client code to perform quite a few
  // extra reads - that's intentional.
  if (bytes_to_read > kCompleteReadThresholdBytes) {
    bytes_to_read = bytes_to_read / 2;
    bytes_to_read = std::max(kCompleteReadThresholdBytes, bytes_to_read);
  }
  file_.read(reinterpret_cast<char*>(buffer_out), bytes_to_read);
  if (!file_) {
    Exit("file_.read() failed");
  }
  ZX_DEBUG_ASSERT(static_cast<uint64_t>(file_.tellg()) == cursor_position_ + bytes_to_read);
  *bytes_read_out = bytes_to_read;
  // InStream::ReadBytes() takes care of advancing cursor_position_.
  return ZX_OK;
}

zx_status_t InStreamFile::ResetToStartInternal(zx::time just_fail_deadline) {
  // This sub-class doesn't envorce just_fail_deadline for now.
  (void)just_fail_deadline;

  file_.seekg(0, std::ios::beg);
  if (!file_) {
    Exit("file_.seekg(0, beg) failed");
  }
  cursor_position_ = 0;

  return ZX_OK;
}
