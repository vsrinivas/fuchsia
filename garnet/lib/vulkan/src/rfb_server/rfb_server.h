// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RFB_SERVER_H_
#define RFB_SERVER_H_

#include <stdint.h>

#include "src/lib/files/unique_fd.h"

// This is a basic RFB server with minimal error-checking and no support for
// compression or input events.
class RFBServer {
 public:
  bool Initialize(uint32_t width, uint32_t height, uint32_t port);

  void WaitForFramebufferUpdate();
  void StartUpdate();
  bool SendBytes(const void* data, uint32_t length);

 private:
  int ReadEntireMessage(void* data, uint32_t size);

  fbl::unique_fd fd_;
  uint32_t width_;
  uint32_t height_;
  bool initialization_attempted_ = false;
  bool initialization_succeeded_ = false;
};

#endif  // RFB_SERVER_H_
