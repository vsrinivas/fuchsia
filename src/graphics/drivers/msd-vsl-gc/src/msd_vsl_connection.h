// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_VSL_CONNECTION_H
#define MSD_VSL_CONNECTION_H

#include <memory>

#include "address_space.h"
#include "magma_util/macros.h"
#include "mapped_batch.h"
#include "msd.h"

class MsdVslConnection {
 public:
  class Owner {
   public:
    // If |do_flush| is true, a flush TLB command will be queued before the batch commands.
    virtual magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch, bool do_flush) = 0;
  };

  MsdVslConnection(Owner* owner, std::shared_ptr<AddressSpace> address_space,
                   msd_client_id_t client_id)
      : owner_(owner), address_space_(std::move(address_space)), client_id_(client_id) {}

  magma::Status MapBufferGpu(std::shared_ptr<MsdVslBuffer> buffer, uint64_t gpu_va,
                             uint64_t page_offset, uint64_t page_count);

  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> mapped_batch) {
    return owner_->SubmitBatch(std::move(mapped_batch), false /* do_flush */);
  }

  msd_client_id_t client_id() { return client_id_; }

  std::shared_ptr<AddressSpace> address_space() { return address_space_; }

 private:
  Owner* owner_;
  std::shared_ptr<AddressSpace> address_space_;
  msd_client_id_t client_id_;
};

class MsdVslAbiConnection : public msd_connection_t {
 public:
  MsdVslAbiConnection(std::shared_ptr<MsdVslConnection> ptr) : ptr_(std::move(ptr)) {
    magic_ = kMagic;
  }

  static MsdVslAbiConnection* cast(msd_connection_t* connection) {
    DASSERT(connection);
    DASSERT(connection->magic_ == kMagic);
    return static_cast<MsdVslAbiConnection*>(connection);
  }

  std::shared_ptr<MsdVslConnection> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdVslConnection> ptr_;
  static const uint32_t kMagic = 0x636f6e6e;  // "conn" (Connection)
};

#endif  // MSD_VSL_CONNECTION_H
