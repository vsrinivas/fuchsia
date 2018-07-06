// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_ARM_CONNECTION_H
#define MSD_ARM_CONNECTION_H

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "address_space.h"
#include "gpu_mapping.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "magma_util/macros.h"
#include "msd.h"
#include "msd_arm_atom.h"
#include "msd_arm_buffer.h"
#include "msd_arm_semaphore.h"

struct magma_arm_mali_atom;

// This can only be accessed on the connection thread.
class MsdArmConnection : public std::enable_shared_from_this<MsdArmConnection>,
                         public GpuMapping::Owner,
                         public AddressSpace::Owner {
public:
    class Owner {
    public:
        virtual void ScheduleAtom(std::shared_ptr<MsdArmAtom> atom) = 0;
        virtual void CancelAtoms(std::shared_ptr<MsdArmConnection> connection) = 0;
        virtual AddressSpaceObserver* GetAddressSpaceObserver() = 0;
        virtual ArmMaliCacheCoherencyStatus cache_coherency_status()
        {
            return kArmMaliCacheCoherencyNone;
        }
        virtual magma::PlatformBusMapper* GetBusMapper() = 0;
    };

    static std::shared_ptr<MsdArmConnection> Create(msd_client_id_t client_id, Owner* owner);

    virtual ~MsdArmConnection();

    msd_client_id_t client_id() { return client_id_; }

    AddressSpace* address_space_for_testing() FXL_NO_THREAD_SAFETY_ANALYSIS
    {
        return address_space_.get();
    }
    const AddressSpace* const_address_space() const FXL_NO_THREAD_SAFETY_ANALYSIS
    {
        return address_space_.get();
    }

    // GpuMapping::Owner implementation.
    bool RemoveMapping(uint64_t gpu_va) override;
    bool UpdateCommittedMemory(GpuMapping* mapping) override;

    bool AddMapping(std::unique_ptr<GpuMapping> mapping);
    // If |atom| is a soft atom, then the first element from
    // |signal_semaphores| will be removed and used for it.
    bool ExecuteAtom(volatile magma_arm_mali_atom* atom,
                     std::deque<std::shared_ptr<magma::PlatformSemaphore>>* signal_semaphores);

    void SetNotificationCallback(msd_connection_notification_callback_t callback, void* token);
    void SendNotificationData(MsdArmAtom* atom, ArmMaliResultCode result_code);
    void MarkDestroyed();

    // Called only on device thread.
    void set_address_space_lost() { address_space_lost_ = true; }
    bool address_space_lost() const { return address_space_lost_; }

    AddressSpaceObserver* GetAddressSpaceObserver() override
    {
        return owner_->GetAddressSpaceObserver();
    }
    std::shared_ptr<AddressSpace::Owner> GetSharedPtr() override { return shared_from_this(); }

    bool PageInMemory(uint64_t address);
    bool CommitMemoryForBuffer(MsdArmBuffer* buffer, uint64_t page_offset, uint64_t page_count);

private:
    static const uint32_t kMagic = 0x636f6e6e; // "conn" (Connection)

    MsdArmConnection(msd_client_id_t client_id, Owner* owner);

    bool Init();

    magma::PlatformBusMapper* GetBusMapper() override { return owner_->GetBusMapper(); }

    msd_client_id_t client_id_;
    std::mutex address_lock_;
    FXL_PT_GUARDED_BY(address_lock_) std::unique_ptr<AddressSpace> address_space_;
    // Map GPU va to a mapping.
    FXL_GUARDED_BY(address_lock_) std::map<uint64_t, std::unique_ptr<GpuMapping>> gpu_mappings_;

    Owner* owner_;

    // Modified and accessed only from device thread.
    bool address_space_lost_ = false;

    std::mutex callback_lock_;
    msd_connection_notification_callback_t callback_;
    void* token_ = {};
    std::shared_ptr<MsdArmAtom> outstanding_atoms_[256];
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
