// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_BUFFER_H
#define MSD_INTEL_BUFFER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "magma_util/macros.h"
#include "msd.h"
#include "platform_buffer.h"
#include "platform_event.h"
#include "types.h"

class AddressSpace;

class MsdIntelBuffer {
 public:
  static std::unique_ptr<MsdIntelBuffer> Import(uint32_t handle);
  static std::unique_ptr<MsdIntelBuffer> Create(uint64_t size, const char* name);

  magma::PlatformBuffer* platform_buffer() {
    DASSERT(platform_buf_);
    return platform_buf_.get();
  }

 private:
  MsdIntelBuffer(std::unique_ptr<magma::PlatformBuffer> platform_buf);

  std::unique_ptr<magma::PlatformBuffer> platform_buf_;
};

class MsdIntelAbiBuffer : public msd_buffer_t {
 public:
  MsdIntelAbiBuffer(std::shared_ptr<MsdIntelBuffer> ptr) : ptr_(std::move(ptr)) { magic_ = kMagic; }

  static MsdIntelAbiBuffer* cast(msd_buffer_t* buf) {
    DASSERT(buf);
    DASSERT(buf->magic_ == kMagic);
    return static_cast<MsdIntelAbiBuffer*>(buf);
  }
  std::shared_ptr<MsdIntelBuffer> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdIntelBuffer> ptr_;
  static const uint32_t kMagic = 0x62756666;  // "buff"
};

#endif  // MSD_INTEL_BUFFER_H
