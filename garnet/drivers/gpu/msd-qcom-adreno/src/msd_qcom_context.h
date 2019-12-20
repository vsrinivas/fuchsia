// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_CONTEXT_H
#define MSD_QCOM_CONTEXT_H

#include <msd.h>

#include <memory>

#include <magma_util/macros.h>

#include "address_space.h"

class MsdQcomContext {};

class MsdQcomAbiContext : public msd_context_t {
 public:
  MsdQcomAbiContext(std::shared_ptr<MsdQcomContext> ptr) : ptr_(std::move(ptr)) { magic_ = kMagic; }

  static MsdQcomAbiContext* cast(msd_context_t* context) {
    DASSERT(context);
    DASSERT(context->magic_ == kMagic);
    return static_cast<MsdQcomAbiContext*>(context);
  }
  std::shared_ptr<MsdQcomContext> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdQcomContext> ptr_;
  static const uint32_t kMagic = 0x63747874;  // "ctxt"
};

#endif  // MSD_QCOM_CONTEXT_H
