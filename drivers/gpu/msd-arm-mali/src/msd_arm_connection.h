// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_CONNECTION_H
#define MSD_ARM_CONNECTION_H

#include "magma_util/macros.h"
#include "msd.h"

#include <memory>

class ClientContext;

class MsdArmConnection {
public:
    static std::unique_ptr<MsdArmConnection> Create(msd_client_id_t client_id);

    MsdArmConnection(msd_client_id_t client_id) : client_id_(client_id) {}

    virtual ~MsdArmConnection() {}

    msd_client_id_t client_id() { return client_id_; }

private:
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)

    msd_client_id_t client_id_;
};

class MsdArmAbiConnection : public msd_connection_t {
public:
    MsdArmAbiConnection(std::shared_ptr<MsdArmConnection> ptr) : ptr_(std::move(ptr))
    {
        magic_ = kMagic;
    }

    static MsdArmAbiConnection* cast(msd_connection_t* connection)
    {
        DASSERT(connection);
        DASSERT(connection->magic_ == kMagic);
        return static_cast<MsdArmAbiConnection*>(connection);
    }

    std::shared_ptr<MsdArmConnection> ptr() { return ptr_; }

private:
    std::shared_ptr<MsdArmConnection> ptr_;
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)
};

#endif // MSD_ARM_CONNECTION_H
