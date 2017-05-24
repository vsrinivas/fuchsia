// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef _DISPLAY_PIPE_BUFFER_H_
#define _DISPLAY_PIPE_BUFFER_H_

#include <stdint.h>

#include <mx/event.h>
#include <mx/vmo.h>

struct Buffer {
 public:
  ~Buffer();

  void Fill(uint8_t r, uint8_t g, uint8_t b);

  void Reset();
  void Signal();

  const mx::event& acqure_fence() { return acquire_fence_; }
  const mx::event& release_fence() { return release_fence_; }

  void dupAcquireFence(mx::event *result) {
     acquire_fence_.duplicate(MX_RIGHT_SAME_RIGHTS, result);
  }

  void dupReleaseFence(mx::event *result) {
     release_fence_.duplicate(MX_RIGHT_SAME_RIGHTS, result);
  }

  void dupVmo(mx::vmo *result) {
     vmo_.duplicate(MX_RIGHT_SAME_RIGHTS, result);
  }

  static Buffer *NewBuffer(uint32_t width, uint32_t height);

 private:
  Buffer() {};

  mx::vmo vmo_;
  uint32_t *pixels_;
  uint64_t size_;
  uint32_t width_;
  uint32_t height_;

  mx::event acquire_fence_;
  mx::event release_fence_;
};

#endif  // _DISPLAY_PIPE_BUFFER_H_

