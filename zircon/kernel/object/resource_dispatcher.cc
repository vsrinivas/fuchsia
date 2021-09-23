// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/resource_dispatcher.h"

#include <inttypes.h>
#include <lib/counters.h>
#include <string.h>
#include <trace.h>
#include <zircon/rights.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <fbl/alloc_checker.h>
#include <kernel/auto_lock.h>
#include <kernel/range_check.h>
#include <pretty/cpp/sizes.h>
#include <vm/vm.h>

using pretty::FormattedBytes;

#define LOCAL_TRACE 0

KCOUNTER(root_resource_created, "resource.root.created")
KCOUNTER(mmio_resource_created, "resource.mmio.created")
KCOUNTER(irq_resource_created, "resource.irq.created")
KCOUNTER(ioport_resource_created, "resource.ioport.created")
KCOUNTER(smc_resource_created, "resource.smc.created")
KCOUNTER(system_resource_created, "resource.system.created")
KCOUNTER(dispatcher_resource_create_count, "dispatcher.resource.create")
KCOUNTER(dispatcher_resource_destroy_count, "dispatcher.resource.destroy")

constexpr size_t kFlagLen = 6;
// Utility function to format the flags into a user-readable string.
void flags_to_string(uint32_t flags, char str[kFlagLen]) {
  memset(str, 0, kFlagLen);
  uint8_t pos = 0;
  if (flags & ZX_RSRC_FLAG_EXCLUSIVE) {
    str[pos++] = 'x';
  }
  str[kFlagLen - 1] = '\0';
}

const char* kKindLabels[ZX_RSRC_KIND_COUNT] = {
    "mmio", "irq", "ioport", "root", "smc", "system",
};

static const char* kind_to_string(uint8_t kind) {
  ZX_ASSERT(kind < ZX_RSRC_KIND_COUNT);
  return kKindLabels[kind];
}

// Storage for static members of ResourceDispatcher
ResourceDispatcher::ResourceStorage ResourceDispatcher::static_storage_;
RegionAllocator::RegionPool::RefPtr ResourceDispatcher::region_pool_;
const char* kLogTag = "Resources:";

// The Create() method here only validates exclusive allocations because
// the kernel is permitted to create shared resources without restriction.
// Validation of parent handles is handled at the syscall boundary in the
// implementation for |zx_resource_create|.
zx_status_t ResourceDispatcher::Create(KernelHandle<ResourceDispatcher>* handle,
                                       zx_rights_t* rights, zx_rsrc_kind_t kind, uint64_t base,
                                       size_t size, uint32_t flags,
                                       const char name[ZX_MAX_NAME_LEN], ResourceStorage* storage) {
  Guard<Mutex> guard{ResourcesLock::Get()};
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

  // Use the local static bookkeeping for system resources unless mocks are passed in.
  if (storage == nullptr) {
    storage = &static_storage_;
  }

  RegionAllocator::Region::UPtr region_uptr = nullptr;
  switch (kind) {
    case ZX_RSRC_KIND_ROOT:
      // It does not make sense for an abstract resource type to have a base/size tuple
      if (base || size) {
        return ZX_ERR_INVALID_ARGS;
      }
      break;
    default:
      // If we have not assigned a region pool to our allocator yet, then we are not
      // yet initialized and should return ZX_ERR_BAD_STATE.
      if (!storage->rallocs[kind].HasRegionPool()) {
        return ZX_ERR_BAD_STATE;
      }

      zx_status_t status =
          storage->rallocs[kind].GetRegion({.base = base, .size = size}, region_uptr);
      if (status != ZX_OK) {
        LTRACEF("%s couldn't pull the resource [%#lx, %#lx) out of %s: %d\n", kLogTag, base,
                base + size, kind_to_string(kind), status);
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
      if (kind != rsrc.get_kind()) {
        return ZX_OK;
      }

      if (Intersects(base, size, rsrc.get_base(), rsrc.get_size())) {
        return ZX_ERR_NOT_FOUND;
      }

      return ZX_OK;
    };
    zx_status_t status = ResourceDispatcher::ForEachResourceLocked(callback, storage);
    if (status != ZX_OK) {
      return status;
    }
  }

  // We've passed the first hurdle, so it's time to construct the dispatcher
  // itself. The constructor will handle adding itself to the shared list if
  // necessary.
  fbl::AllocChecker ac;
  KernelHandle new_handle(fbl::AdoptRef(
      new (&ac) ResourceDispatcher(kind, base, size, flags, ktl::move(region_uptr), storage)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (name != nullptr) {
    new_handle.dispatcher()->set_name(name, ZX_MAX_NAME_LEN);
  }

  *rights = default_rights();
  *handle = ktl::move(new_handle);

  LTRACEF("%s %s [%#lx, %#lx) resource created.\n", kLogTag, kind_to_string(kind), base,
          base + size);
  return ZX_OK;
}

// The CreateRootRanged() method here does not validate exclusive allocations because
// it represents a ranged resource with all valid regions.
// Validation of regions is handled at the syscall boundary in the
// implementation for |zx_resource_create|.
zx_status_t ResourceDispatcher::CreateRangedRoot(KernelHandle<ResourceDispatcher>* handle,
                                                 zx_rights_t* rights, zx_rsrc_kind_t kind,
                                                 const char name[ZX_MAX_NAME_LEN],
                                                 ResourceStorage* storage) {
  Guard<Mutex> guard{ResourcesLock::Get()};
  if (kind >= ZX_RSRC_KIND_COUNT) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Use the local static bookkeeping for system resources unless mocks are passed in.
  if (storage == nullptr) {
    storage = &static_storage_;
  }

  // Abstract resource types have no size. Ranged resource types are given infinite size to
  // indicate that they represent all valid ranges.
  switch (kind) {
    // TODO(smpham): remove this when root resource is removed.
    case ZX_RSRC_KIND_ROOT:
      // The Create() method should be used for making these resource kinds.
      return ZX_ERR_WRONG_TYPE;
    default:
      // If we have not assigned a region pool to our allocator yet, then we are not
      // yet initialized and should return ZX_ERR_BAD_STATE.
      if (!storage->rallocs[kind].HasRegionPool()) {
        return ZX_ERR_BAD_STATE;
      }
  }

  // We've passed the first hurdle, so it's time to construct the dispatcher
  // itself. The constructor will handle adding itself to the shared list if
  // necessary.
  fbl::AllocChecker ac;
  KernelHandle new_handle(
      fbl::AdoptRef(new (&ac) ResourceDispatcher(kind, 0, 0, 0, nullptr, storage)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if (name != nullptr) {
    new_handle.dispatcher()->set_name(name, ZX_MAX_NAME_LEN);
  }

  *rights = default_rights();
  *handle = ktl::move(new_handle);

  LTRACEF("%s [%u] ranged root resource created.\n", kLogTag, kind);
  return ZX_OK;
}

ResourceDispatcher::ResourceDispatcher(zx_rsrc_kind_t kind, uint64_t base, uint64_t size,
                                       uint32_t flags, RegionAllocator::Region::UPtr&& region,
                                       ResourceStorage* storage)
    : kind_(kind),
      base_(base),
      size_(size),
      flags_(flags),
      resource_list_(&storage->resource_list) {
  kcounter_add(dispatcher_resource_create_count, 1);

  if (flags_ & ZX_RSRC_FLAG_EXCLUSIVE) {
    exclusive_region_ = ktl::move(region);
  }

  switch (kind_) {
    case ZX_RSRC_KIND_ROOT:
      kcounter_add(root_resource_created, 1);
      break;
    case ZX_RSRC_KIND_MMIO:
      kcounter_add(mmio_resource_created, 1);
      break;
    case ZX_RSRC_KIND_IRQ:
      kcounter_add(irq_resource_created, 1);
      break;
    case ZX_RSRC_KIND_IOPORT:
      kcounter_add(ioport_resource_created, 1);
      break;
    case ZX_RSRC_KIND_SMC:
      kcounter_add(smc_resource_created, 1);
      break;
    case ZX_RSRC_KIND_SYSTEM:
      kcounter_add(system_resource_created, 1);
      break;
  }
  resource_list_->push_back(this);
}

ResourceDispatcher::~ResourceDispatcher() {
  kcounter_add(dispatcher_resource_destroy_count, 1);

  // exclusive allocations will be released when the uptr goes out of scope,
  // shared need to be removed from |all_shared_list_|
  Guard<Mutex> guard{ResourcesLock::Get()};
  char name[ZX_MAX_NAME_LEN];
  get_name(name);
  resource_list_->erase(*this);
}

zx_status_t ResourceDispatcher::InitializeAllocator(zx_rsrc_kind_t kind, uint64_t base, size_t size,
                                                    ResourceStorage* storage) {
  DEBUG_ASSERT(kind < ZX_RSRC_KIND_COUNT);
  DEBUG_ASSERT(size > 0);

  // Static methods need to check for mocks manually.
  if (storage == nullptr) {
    storage = &static_storage_;
  }

  Guard<Mutex> guard{ResourcesLock::Get()};
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

  status = storage->rallocs[kind].SetRegionPool(region_pool_);
  if (status != ZX_OK) {
    return status;
  }

  // Add the initial address space specified by the platform to the region allocator.
  // This will be used for verifying both shared and exclusive allocations of address
  // space.
  status = storage->rallocs[kind].AddRegion({.base = base, .size = size});
  LTRACEF("%s added [%#lx, %zu) size = %#lx to %s allocator: %d\n", kLogTag, base, base + size,
          size, kind_to_string(kind), status);
  return status;
}

void ResourceDispatcher::DumpResources() {
  zx_rsrc_kind_t kind;
  auto callback = [&](const ResourceDispatcher& r) -> zx_status_t {
    // exit early so we can print the list in a grouped format
    // without adding overhead to the list management.
    if (r.get_kind() != kind) {
      return ZX_OK;
    }

    char region[32]{};
    char name[ZX_MAX_NAME_LEN]{};
    char flag_str[kFlagLen]{};
    r.get_name(name);
    flags_to_string(r.get_flags(), flag_str);
    printf("%32s  ", name);
    printf("\t%10s  ", kind_to_string(r.get_kind()));
    printf("%8s  ", flag_str);
    printf("\t%-#10lx  ", r.get_koid());

    if (r.get_size() && r.get_kind() != ZX_RSRC_KIND_ROOT && r.get_kind() != ZX_RSRC_KIND_SYSTEM) {
      // Only MMIO should be printed as bytes.
      if (r.get_kind() == ZX_RSRC_KIND_MMIO) {
        printf("\t%8s  ", FormattedBytes(r.get_size()).c_str());
      } else {
        // And only resources with a size should print one.
        printf("\t%#8zx  ", r.get_size());
      }
      // If we had a size then we can print a region.
      snprintf(region, sizeof(region), "[%#lx, %#lx)", r.get_base(), r.get_base() + r.get_size());
    } else {
      printf("\t%8s  ", " ");
    }

    printf("%-32s\n", region);
    return ZX_OK;
  };

  printf("Resources in use:\n");
  printf("%32s  ", "name");
  printf("\t%10s  ", "type");
  printf("%8s  ", "flags");
  printf("\t%-10s  ", "koid");
  printf("\t%8s  ", "size");
  printf("%-32s\n", "region");

  // Values determined by staring at it until it looked good enough.
  printf(
      "        "
      "-------------------------------------------------------------------------------------------"
      "\n");
  for (kind = 0; kind < ZX_RSRC_KIND_COUNT; kind++) {
    ResourceDispatcher::ForEachResource(callback);
  }
}

void ResourceDispatcher::DumpAllocators() {
  Guard<Mutex> guard{ResourcesLock::Get()};
  printf("Available regions:\n");
  printf("%32s  ", "type");
  printf("\t%8s  ", "size");
  printf("region\n");
  printf(
      "        "
      "-------------------------------------------------------------------------------------------"
      "\n");
  auto print_func = [](uint32_t kind, const ralloc_region_t* region) -> bool {
    printf("%32s  ", kind_to_string(kind));
    if (kind == ZX_RSRC_KIND_MMIO) {
      printf("\t%8s  ", FormattedBytes(region->size).c_str());
    } else {
      printf("\t%#8lx  ", region->size);
    }
    printf("[%#lx, %#lx)\n", region->base, region->base + region->size);
    return true;
  };

  for (auto& kind : {ZX_RSRC_KIND_MMIO, ZX_RSRC_KIND_IRQ, ZX_RSRC_KIND_IOPORT}) {
    static_storage_.rallocs[kind].WalkAvailableRegions(
        [&kind, &print_func](const ralloc_region_t* region) { return print_func(kind, region); });
  }
}

#include <lib/console.h>

static int cmd_resource(int argc, const cmd_args* argv, uint32_t flags) {
  ResourceDispatcher::DumpResources();
  ResourceDispatcher::DumpAllocators();
  return true;
}

STATIC_COMMAND_START
STATIC_COMMAND("resource", "Inspect physical address space resource allocations", &cmd_resource)
STATIC_COMMAND_END(resource)
