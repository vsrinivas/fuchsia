// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSI_BUFFER_H
#define MSD_VSI_BUFFER_H

#include "magma_util/macros.h"
#include "msd.h"
#include "platform_buffer.h"

class MsdVsiBuffer {
 public:
  static std::unique_ptr<MsdVsiBuffer> Import(uint32_t handle);
  static std::unique_ptr<MsdVsiBuffer> Create(uint64_t size, const char* name);

  magma::PlatformBuffer* platform_buffer() {
    DASSERT(platform_buf_);
    return platform_buf_.get();
  }

  MsdVsiBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf)
      : platform_buf_(std::move(platform_buf)) {}

 private:
  std::unique_ptr<magma::PlatformBuffer> platform_buf_;
};

class MsdVsiAbiBuffer : public msd_buffer_t {
 public:
  MsdVsiAbiBuffer(std::shared_ptr<MsdVsiBuffer> ptr) : ptr_(std::move(ptr)) { magic_ = kMagic; }

  static MsdVsiAbiBuffer* cast(msd_buffer_t* buf) {
    DASSERT(buf);
    DASSERT(buf->magic_ == kMagic);
    return static_cast<MsdVsiAbiBuffer*>(buf);
  }
  std::shared_ptr<MsdVsiBuffer> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdVsiBuffer> ptr_;
  static const uint32_t kMagic = 0x62756666;  // "buff"
};

#endif  // MSD_VSI_BUFFER_H
