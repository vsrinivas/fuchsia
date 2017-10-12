// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_CONNECTION_H
#define MSD_ARM_CONNECTION_H

#include <map>
#include <memory>

#include "gpu_mapping.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "msd_arm_atom.h"

class AddressSpace;
struct magma_arm_mali_atom;

// This can only be accessed on the connection thread.
class MsdArmConnection : public std::enable_shared_from_this<MsdArmConnection>,
                         public GpuMapping::Owner {
public:
    class Owner {
    public:
        virtual void ScheduleAtom(std::unique_ptr<MsdArmAtom> atom) = 0;
    };

    static std::shared_ptr<MsdArmConnection> Create(msd_client_id_t client_id, Owner* owner);

    MsdArmConnection(msd_client_id_t client_id, std::unique_ptr<AddressSpace> address_space,
                     Owner* owner);

    virtual ~MsdArmConnection();

    msd_client_id_t client_id() { return client_id_; }

    AddressSpace* address_space() { return address_space_.get(); }

    bool RemoveMapping(uint64_t gpu_va) override;

    bool AddMapping(std::unique_ptr<GpuMapping> mapping);
    void ExecuteAtom(volatile magma_arm_mali_atom* atom);

private:
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)

    msd_client_id_t client_id_;
    std::unique_ptr<AddressSpace> address_space_;
    // Map GPU va to a mapping.
    std::map<uint64_t, std::unique_ptr<GpuMapping>> gpu_mappings_;

    Owner* owner_;
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
