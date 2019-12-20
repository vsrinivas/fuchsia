// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_QCOM_CONNECTION_H
#define MSD_QCOM_CONNECTION_H

#include <msd.h>

#include <memory>

#include <magma_util/macros.h>

class MsdQcomConnection {
 public:
  MsdQcomConnection(msd_client_id_t client_id) : client_id_(client_id) {}

  msd_client_id_t client_id() { return client_id_; }

 private:
  msd_client_id_t client_id_;
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
