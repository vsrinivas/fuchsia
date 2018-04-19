// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/resource_dispatcher.h>

#include <zircon/rights.h>
#include <zircon/syscalls/resource.h>
#include <fbl/alloc_checker.h>
#include <pretty/sizes.h>
#include <inttypes.h>
#include <kernel/auto_lock.h>
#include <kernel/range_check.h>
#include <string.h>
#include <vm/vm.h>
#include <trace.h>

#define LOCAL_TRACE 0

// Storage for static members of ResourceDispatcher
fbl::Mutex ResourceDispatcher::resources_lock_;
RegionAllocator ResourceDispatcher::static_rallocs_[ZX_RSRC_KIND_COUNT];
ResourceDispatcher::ResourceList ResourceDispatcher::static_resource_list_;
RegionAllocator::RegionPool::RefPtr ResourceDispatcher::region_pool_;
const char* kLogTag = "Resources:";

zx_status_t ResourceDispatcher::Create(fbl::RefPtr<ResourceDispatcher>* dispatcher,
                                       zx_rights_t* rights,
                                       uint32_t kind,
                                       uint64_t base,
                                       size_t size,
                                       uint32_t flags,
                                       const char name[ZX_MAX_NAME_LEN],
                                       RegionAllocator rallocs[ZX_RSRC_KIND_COUNT],
                                       ResourceList* resource_list) {
    fbl::AutoLock lock(&resources_lock_);
    if (kind >= ZX_RSRC_KIND_COUNT || (flags & ZX_RSRC_FLAGS_MASK) != flags) {
        return ZX_ERR_INVALID_ARGS;
    }

    // The first thing we need to do for any resource is ensure that it has not
    // been exclusively reserved. If GetRegion succeeds and we have a region
    // uptr then in the case of an exclusive resource we'll move it into the
    // class instance. Otherwise, the resource is shared and we'll release it
    // back to the allocator since we only used it to verify it existed in the
    // allocator.
    //
    // TODO: Hypervisor resources should be represented in some other capability
    // object because they represent a binary permission rather than anything
    // more finely grained. It will work properly here because the base/size of a
    // hypervisor resource is never checked, but it's a workaround until a
    // proper capability exists for it.
    zx_status_t status;
    RegionAllocator::Region::UPtr region_uptr = nullptr;
    switch (kind) {
    case ZX_RSRC_KIND_ROOT:
    case ZX_RSRC_KIND_HYPERVISOR:
        // It does not make sense for an abstract resource type to have a base/size tuple
        if (base || size) {
            return ZX_ERR_INVALID_ARGS;
        }
        break;
    default:
        status = rallocs[kind].GetRegion({ .base = base, .size = size }, region_uptr);
        if (status != ZX_OK) {
            LTRACEF("%s couldn't pull the resource out of the ralloc %d\n", kLogTag, status);
            return status;
        }
    }

    // If the allocation is exclusive then a check needs to be made to ensure
    // that no shared allocation already exists and/or overlaps. Shared
    // resources don't need to do so because grabbing the exclusive region above
    // (temporarily) ensures they are valid allocations. If this check fails
    // then the region above will be released back to the pool anyway.
    if (flags & ZX_RSRC_FLAG_EXCLUSIVE) {
        auto callback = [&](const ResourceDispatcher& rsrc) {
            LTRACEF("%s walking resources, found [%u, %#lx, %zu]\n", kLogTag,  rsrc.get_kind(),
                    rsrc.get_base(), rsrc.get_size());
            if (kind != rsrc.get_kind()) {
                return ZX_OK;
            }

            if (Intersects(base, size, rsrc.get_base(), rsrc.get_size())) {
                LTRACEF("%s [%#lx, %zu] intersects with [%#lx, %zu] found in list!\n", kLogTag,
                        base, size, rsrc.get_base(), rsrc.get_size());
                return ZX_ERR_NOT_FOUND;
            }

            return ZX_OK;
        };
        LTRACEF("%s scanning resource list for [%u, %#lx, %zu]\n", kLogTag, kind, base, size);
        zx_status_t status = ResourceDispatcher::ForEachResourceLocked(callback, resource_list);
        if (status != ZX_OK) {
            return status;
        }
    }

    // We've passd the first hurdle, so it's time to construct the dispatcher
    // itself.  The constructor will handle adding itself to the shared list if
    // necessary.
    fbl::AllocChecker ac;
    auto disp = fbl::AdoptRef(new (&ac) ResourceDispatcher(kind, base, size, flags,
                                                           fbl::move(region_uptr),
                                                           rallocs, resource_list));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    if (name != nullptr) {
        disp->set_name(name, ZX_MAX_NAME_LEN);
    }

    *rights = ZX_DEFAULT_RESOURCE_RIGHTS;
    *dispatcher = fbl::move(disp);

    LTRACEF("%s [%u, %#lx, %zu] resource created.\n", kLogTag, kind, base, size);
    return ZX_OK;

}

ResourceDispatcher::ResourceDispatcher(uint32_t kind,
                                       uint64_t base,
                                       uint64_t size,
                                       uint32_t flags,
                                       RegionAllocator::Region::UPtr&& region,
                                       RegionAllocator rallocs[ZX_RSRC_KIND_COUNT],
                                       ResourceList* resource_list)
    : kind_(kind), base_(base), size_(size), flags_(flags),
        resource_list_(resource_list) {
    if (flags_ & ZX_RSRC_FLAG_EXCLUSIVE) {
        exclusive_region_ = fbl::move(region);
    }

    resource_list_->push_back(this);
}

ResourceDispatcher::~ResourceDispatcher() {
    // exclusive allocations will be released when the uptr goes out of scope,
    // shared need to be removed from |all_shared_list_|
    fbl::AutoLock lock(&resources_lock_);
    char name[ZX_MAX_NAME_LEN];
    get_name(name);
    resource_list_->erase(*this);
}

zx_status_t ResourceDispatcher::InitializeAllocator(uint32_t kind,
                                                    uint64_t base,
                                                    size_t size,
                                                    RegionAllocator rallocs[ZX_RSRC_KIND_COUNT]) {
    DEBUG_ASSERT(kind < ZX_RSRC_KIND_COUNT);
    DEBUG_ASSERT(size > 0);

    fbl::AutoLock lock(&resources_lock_);
    zx_status_t status;

    // This method should only be called for resource kinds with bookkeeping.
    if (kind >= ZX_RSRC_KIND_COUNT) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Create the initial region pool if necessary. Its storage is allocated in this cpp file
    if (region_pool_ == nullptr) {
        region_pool_ = RegionAllocator::RegionPool::Create(kMaxRegionPoolSize);
    }

    // Failure to allocate this early in boot is a critical error
    DEBUG_ASSERT(region_pool_);

    status = rallocs[kind].SetRegionPool(region_pool_);
    if (status != ZX_OK) {
        return status;
    }


    // Add the initial address space specified by the platform to the region allocator.
    // This will be used for verifying both shared and exclusive allocations of address
    // space.
    status = rallocs[kind].AddRegion({ .base = base, .size = size });
    LTRACEF("%s added [%#lx, %zu] to kind %u in allocator %p: %d\n", kLogTag, base, size, kind, rallocs, status);
    return status;
}

// Size specifiers for the debug output
constexpr int kTypeLen = 10;
constexpr int kFlagLen = 6;
constexpr int kNameLen = ZX_MAX_NAME_LEN - 1;
constexpr int kNumLen = 16;
constexpr int kPrettyLen = 8;

// Utility function to format the flags into a user-readable string.
static constexpr void flags_to_string(uint32_t flags, char str[kFlagLen]) {
    str[0] = ' ';
    str[1] = ' ';
    str[2] = ' ';
    str[3] = (flags & ZX_RSRC_FLAG_EXCLUSIVE) ? ' ' : 's';
    str[4] = (flags & ZX_RSRC_FLAG_EXCLUSIVE) ? 'x' : ' ';
    str[5] = '\0';
}

static void pad_field(int width) {
    printf("\t%.*s", width, "                        ");
}

void ResourceDispatcher::Dump() {
    uint32_t kind;
    auto callback = [&](const ResourceDispatcher& r) -> zx_status_t {
        char name[ZX_MAX_NAME_LEN];
        char flag_str[kFlagLen];
        char pretty_size[kPrettyLen];

        // exit early so we can print the list in a grouped format
        // without adding overhead to the list management.
        if (r.get_kind() != kind) {
            return ZX_OK;
        }

        // A safety check to make sure we don't need to worry about snprintf edge cases
        r.get_name(name);
        flags_to_string(r.get_flags(), flag_str);

        // IRQs are allocated one at a time, so range display doesn't make much sense.
        switch (r.get_kind()) {
        case ZX_RSRC_KIND_ROOT:
            printf("%.*s",           kTypeLen, "root");
            pad_field(kFlagLen);     // Root has no flags
            printf("\t%.*s",         kNameLen, name);
            printf("\n");
            break;
        case ZX_RSRC_KIND_HYPERVISOR:
            printf("%.*s",           kTypeLen, "hypervisor");
            printf("\t%.*s",         kFlagLen, flag_str);
            printf("\t%.*s",         kNameLen, name);
            printf("\n");
            break;
        case ZX_RSRC_KIND_IRQ:
            printf("%.*s",           kTypeLen, "irq");
            printf("\t%.*s",         kFlagLen, flag_str);
            printf("\t%.*s",         kNameLen, name);
            printf("\t%#.*" PRIxPTR, kNumLen, r.get_base());
            printf("\n");
            break;
        case ZX_RSRC_KIND_IOPORT:
            printf("%.*s",           kTypeLen, "io");
            printf("\t%.*s",         kFlagLen, flag_str);
            printf("\t%.*s",         kNameLen, name);
            printf("\t%#.*" PRIxPTR, kNumLen, r.get_base());
            printf("\t%#.*" PRIxPTR, kNumLen, r.get_base() + r.get_size());
            printf("\t%.*s",         kPrettyLen,
                   format_size(pretty_size, sizeof(pretty_size), r.get_size()));
            printf("\n");
            break;
        case ZX_RSRC_KIND_MMIO:
            printf("%.*s",           kTypeLen, "mmio");
            printf("\t%.*s",         kFlagLen, flag_str);
            printf("\t%.*s",         kNameLen, name);
            printf("\t%#.*" PRIxPTR, kNumLen, r.get_base());
            printf("\t%#.*" PRIxPTR, kNumLen, r.get_base() + r.get_size());
            printf("\t%.*s",         kPrettyLen,
                   format_size(pretty_size, sizeof(pretty_size), r.get_size()));
            printf("\n");
            break;
        }

        return ZX_OK;
    };

    printf("%10s\t%4s\t%31s\t%16s\t%16s\t%8s\n\n", "type", "flags", "name", "start", "end", "size");
    for (kind = 0; kind < ZX_RSRC_KIND_COUNT; kind++) {
        ResourceDispatcher::ForEachResource(callback);
    }
}

#ifdef WITH_LIB_CONSOLE
#include <lib/console.h>

static int cmd_resources(int argc, const cmd_args* argv, uint32_t flags) {
    ResourceDispatcher::Dump();
    return true;
}

STATIC_COMMAND_START
STATIC_COMMAND("resource", "Inspect physical address space resource allocations", &cmd_resources)
STATIC_COMMAND_END(resources);
#endif

