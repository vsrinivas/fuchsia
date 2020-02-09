// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_CONTEXT_H
#define MSD_VSL_CONTEXT_H

#include <memory>

#include "address_space.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "msd_vsl_connection.h"

class MsdVslContext {
 public:
  MsdVslContext(std::weak_ptr<MsdVslConnection> connection,
                std::shared_ptr<AddressSpace> address_space)
      : connection_(connection), address_space_(std::move(address_space)) {}

  std::shared_ptr<AddressSpace> exec_address_space() { return address_space_; }
  std::weak_ptr<MsdVslConnection> connection() { return connection_; }

 private:
  std::weak_ptr<MsdVslConnection> connection_;
  std::shared_ptr<AddressSpace> address_space_;
};

class MsdVslAbiContext : public msd_context_t {
 public:
  MsdVslAbiContext(std::shared_ptr<MsdVslContext> ptr) : ptr_(std::move(ptr)) { magic_ = kMagic; }

  static MsdVslAbiContext* cast(msd_context_t* context) {
    DASSERT(context);
    DASSERT(context->magic_ == kMagic);
    return static_cast<MsdVslAbiContext*>(context);
  }
  std::shared_ptr<MsdVslContext> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdVslContext> ptr_;
  static const uint32_t kMagic = 0x63747874;  // "ctxt"
};

#endif  // MSD_VSL_CONTEXT_H
