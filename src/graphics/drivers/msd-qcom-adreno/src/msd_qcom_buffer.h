// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_BUFFER_H
#define MSD_QCOM_BUFFER_H

#include <msd.h>
#include <platform_buffer.h>

#include <magma_util/macros.h>

class MsdQcomAbiBuffer : public msd_buffer_t {
 public:
  MsdQcomAbiBuffer(std::shared_ptr<magma::PlatformBuffer> ptr) : ptr_(std::move(ptr)) {
    magic_ = kMagic;
  }

  static MsdQcomAbiBuffer* cast(msd_buffer_t* buf) {
    DASSERT(buf);
    DASSERT(buf->magic_ == kMagic);
    return static_cast<MsdQcomAbiBuffer*>(buf);
  }

  std::shared_ptr<magma::PlatformBuffer> ptr() { return ptr_; }

 private:
  std::shared_ptr<magma::PlatformBuffer> ptr_;
  static const uint32_t kMagic = 0x62756666;  // "buff"
};

#endif  // MSD_QCOM_BUFFER_H
