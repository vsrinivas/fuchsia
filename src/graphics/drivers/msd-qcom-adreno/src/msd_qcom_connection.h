// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_CONNECTION_H
#define MSD_QCOM_CONNECTION_H

#include <msd.h>

#include <memory>

#include <magma_util/macros.h>

#include "address_space.h"
#include "platform_bus_mapper.h"

class MsdQcomConnection {
 public:
  class Owner {
   public:
    virtual magma::PlatformBusMapper* GetBusMapper() = 0;
  };

  MsdQcomConnection(Owner* owner, msd_client_id_t client_id,
                    std::unique_ptr<AddressSpace> address_space)
      : owner_(owner), client_id_(client_id), address_space_(std::move(address_space)) {}

  msd_client_id_t client_id() { return client_id_; }

  magma::PlatformBusMapper* GetBusMapper() const { return owner_->GetBusMapper(); }

  std::shared_ptr<AddressSpace> address_space() const { return address_space_; }

 private:
  Owner* owner_;
  msd_client_id_t client_id_;
  std::shared_ptr<AddressSpace> address_space_;
};

class MsdQcomAbiConnection : public msd_connection_t {
 public:
  MsdQcomAbiConnection(std::shared_ptr<MsdQcomConnection> ptr) : ptr_(std::move(ptr)) {
    magic_ = kMagic;
  }

  static MsdQcomAbiConnection* cast(msd_connection_t* connection) {
    DASSERT(connection);
    DASSERT(connection->magic_ == kMagic);
    return static_cast<MsdQcomAbiConnection*>(connection);
  }

  std::shared_ptr<MsdQcomConnection> ptr() { return ptr_; }

 private:
  std::shared_ptr<MsdQcomConnection> ptr_;
  static const uint32_t kMagic = 0x636f6e6e;  // "conn" (Connection)
};

#endif  // MSD_QCOM_CONNECTION_H
