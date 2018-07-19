// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/name.h>
#include <kernel/lockdep.h>
#include <kernel/mutex.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <region-alloc/region-alloc.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/resource.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

class ResourceRecord;

class ResourceDispatcher final : public SoloDispatcher<ResourceDispatcher>,
                                 public fbl::DoublyLinkedListable<ResourceDispatcher*> {
public:
    static constexpr size_t kMaxRegionPoolSize = 64 << 10;

    using ResourceList = fbl::DoublyLinkedList<ResourceDispatcher*>;
    using RefPtr = fbl::RefPtr<ResourceDispatcher>;

    // Creates ResourceDispatcher object representing access rights ta
    // given region of address space from a particular address space allocator, or a root resource
    // granted full access permissions. Only one instance of the root resource is created at boot.
    static zx_status_t Create(ResourceDispatcher::RefPtr* dispatcher,
                              zx_rights_t* rights,
                              uint32_t kind,
                              uint64_t base,
                              size_t size,
                              uint32_t flags,
                              const char name[ZX_MAX_NAME_LEN],
                              RegionAllocator rallocs[ZX_RSRC_KIND_COUNT] = static_rallocs_,
                              ResourceList* resource_list = &static_resource_list_);
    // Initializes the static mmembers used for bookkeeping and storage.
    static zx_status_t InitializeAllocator(uint32_t kind,
                                           uint64_t base,
                                           size_t size,
                                           RegionAllocator rallocs[ZX_RSRC_KIND_COUNT] =
                                               static_rallocs_);
    static void Dump();

    template <typename T>
    static zx_status_t ForEachResource(T func, ResourceList* resource_list = &static_resource_list_)
        TA_EXCL(ResourcesLock::Get()) {
        Guard<fbl::Mutex> guard{ResourcesLock::Get()};
        return ForEachResourceLocked(func, resource_list);
    }

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_RESOURCE; }
    bool has_state_tracker() const final { return true; }

    // Returns a null-terminated name, or the empty string if set_name() has not
    // been called.
    void get_name(char out_name[ZX_MAX_NAME_LEN]) const final {
        name_.get(ZX_MAX_NAME_LEN, out_name);
    }

    // Sets the name of the object. May truncate internally. |size| is the size
    // of the buffer pointed to by |name|.
    zx_status_t set_name(const char* name, size_t size) final {
        return name_.set(name, size);
    }

    uint64_t get_base() const { return base_; }
    size_t get_size() const { return size_; }
    uint32_t get_kind() const { return kind_; }
    uint32_t get_flags() const { return flags_; }
    ~ResourceDispatcher();
private:
    fbl::Canary<fbl::magic("RSRD")> canary_;

    ResourceDispatcher(uint32_t kind,
                       uint64_t base,
                       size_t size,
                       uint32_t flags,
                       RegionAllocator::Region::UPtr&& region,
                       RegionAllocator rallocs[ZX_RSRC_KIND_COUNT],
                       ResourceList* resource_list);

    template <typename T>
    static zx_status_t ForEachResourceLocked(T callback,
                                             ResourceList* resource_list = &static_resource_list_)
        TA_REQ(ResourcesLock::Get()) {
        for (const auto& resource : *resource_list) {
            zx_status_t status = callback(resource);
            if (status != ZX_OK) {
                return status;
            }
        }
        return ZX_OK;
    }

    const uint32_t kind_;
    const uint64_t base_;
    const size_t size_;
    const uint32_t flags_;
    ResourceList* resource_list_;
    fbl::Name<ZX_MAX_NAME_LEN> name_;
    RegionAllocator::Region::UPtr exclusive_region_;

    // Static tracking data structures for physical address space allocations.
    // Exclusive allocations are pulled out of the RegionAllocators, and all
    // allocations are added to |static_resource_list_|. Shared allocations will
    // check that no exclusive reservation exists, but then release the region
    // back to the allocator. Likewise, exclusive allocations will check to
    // ensure that the region has not already been allocated as a shared region
    // by checking the static resource list.
    DECLARE_SINGLETON_MUTEX(ResourcesLock);
    static RegionAllocator static_rallocs_[ZX_RSRC_KIND_COUNT] TA_GUARDED(ResourcesLock::Get());
    static RegionAllocator::RegionPool::RefPtr region_pool_;
    // A single global list is used for all resources so that root and hypervisor resources can
    // still be tracked, and filtering can be done via client tools/commands when displaying
    // the list is concerned.
    static ResourceList static_resource_list_ TA_GUARDED(ResourcesLock::Get());
};
