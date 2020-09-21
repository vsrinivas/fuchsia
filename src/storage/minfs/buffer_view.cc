// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffer_view.h"

namespace minfs {

BaseBufferView::~BaseBufferView() { ZX_ASSERT_MSG(!dirty_, "Flush not called on dirty buffer."); }

BaseBufferView& BaseBufferView::operator=(BaseBufferView&& other) {
  ZX_ASSERT_MSG(!dirty_, "Flush not called on dirty buffer.");
  buffer_ = other.buffer_;
  length_ = other.length_;
  offset_ = other.offset_;
  dirty_ = other.dirty_;
  flusher_ = std::move(other.flusher_);

  other.buffer_ = BufferPtr();
  other.dirty_ = false;
  return *this;
}

zx_status_t BaseBufferView::Flush() {
  if (!dirty_)
    return ZX_OK;
  dirty_ = false;
  return flusher_(this);
}

}  // namespace minfs
