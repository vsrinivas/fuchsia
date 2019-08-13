// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include "allocator.h"
#include "buffer_collection_token.h"
#include "contiguous_pooled_system_ram_memory_allocator.h"
#include "macros.h"

#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <lib/fidl-async-2/simple_binding.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
#include <zircon/device/sysmem.h>

namespace {

class SystemRamMemoryAllocator : public MemoryAllocator {
 public:
  zx_status_t Allocate(uint64_t size, zx::vmo* parent_vmo) override {
    return zx::vmo::create(size, 0, parent_vmo);
  }
  zx_status_t SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo) override {
    // nothing to do here
    return ZX_OK;
  }
  virtual void Delete(zx::vmo parent_vmo) override {
    // ~parent_vmo
  }

  bool CoherencyDomainIsInaccessible() override { return false; }
};

class ContiguousSystemRamMemoryAllocator : public MemoryAllocator {
 public:
  explicit ContiguousSystemRamMemoryAllocator(Owner* parent_device)
      : parent_device_(parent_device) {}

  zx_status_t Allocate(uint64_t size, zx::vmo* parent_vmo) override {
    zx::vmo result_parent_vmo;
    // This code is unlikely to work after running for a while and physical
    // memory is more fragmented than early during boot. The
    // ContiguousPooledSystemRamMemoryAllocator handles that case by keeping
    // a separate pool of contiguous memory.
    zx_status_t status = zx::vmo::create_contiguous(parent_device_->bti(), size, 0, &result_parent_vmo);
    if (status != ZX_OK) {
      DRIVER_ERROR(
          "zx::vmo::create_contiguous() failed - size_bytes: %lu "
          "status: %d",
          size, status);
      zx_info_kmem_stats_t kmem_stats;
      status = zx_object_get_info(get_root_resource(), ZX_INFO_KMEM_STATS, &kmem_stats,
                                  sizeof(kmem_stats), nullptr, nullptr);
      if (status == ZX_OK) {
        DRIVER_ERROR(
            "kmem stats: total_bytes: 0x%lx free_bytes 0x%lx: wired_bytes: 0x%lx vmo_bytes: 0x%lx\n"
            "mmu_overhead_bytes: 0x%lx other_bytes: 0x%lx",
            kmem_stats.total_bytes, kmem_stats.free_bytes, kmem_stats.wired_bytes,
            kmem_stats.vmo_bytes, kmem_stats.mmu_overhead_bytes, kmem_stats.other_bytes);
      }
      // sanitize to ZX_ERR_NO_MEMORY regardless of why.
      status = ZX_ERR_NO_MEMORY;
      return status;
    }
    *parent_vmo = std::move(result_parent_vmo);
    return ZX_OK;
  }
  virtual zx_status_t SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo) override {
    // nothing to do here
    return ZX_OK;
  }
  void Delete(zx::vmo parent_vmo) override {
    // ~vmo
  }

  bool CoherencyDomainIsInaccessible() override { return false; }

 private:
  Owner* const parent_device_;
};

class ExternalMemoryAllocator : public MemoryAllocator {
 public:
  ExternalMemoryAllocator(zx::channel connection, fbl::unique_ptr<async::Wait> wait_for_close)
      : connection_(std::move(connection)), wait_for_close_(std::move(wait_for_close)) {}

  zx_status_t Allocate(uint64_t size, zx::vmo* parent_vmo) override {
    zx::vmo result_vmo;
    zx_status_t status2 = ZX_OK;
    zx_status_t status = fuchsia_sysmem_HeapAllocateVmo(connection_.get(), size, &status2,
                                                        result_vmo.reset_and_get_address());
    if (status != ZX_OK || status2 != ZX_OK) {
      DRIVER_ERROR("HeapAllocate() failed - status: %d status2: %d", status, status2);
      // sanitize to ZX_ERR_NO_MEMORY regardless of why.
      status = ZX_ERR_NO_MEMORY;
      return status;
    }

    *parent_vmo = std::move(result_vmo);
    return ZX_OK;
  }

  zx_status_t SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo) override {
    zx::vmo child_vmo_copy;
    zx_status_t status = child_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &child_vmo_copy);
    if (status != ZX_OK) {
      DRIVER_ERROR("duplicate() failed - status: %d", status);
      // sanitize to ZX_ERR_NO_MEMORY regardless of why.
      status = ZX_ERR_NO_MEMORY;
      return status;
    }

    zx_status_t status2;
    uint64_t id;
    status =
        fuchsia_sysmem_HeapCreateResource(connection_.get(), child_vmo_copy.release(), &status2, &id);
    if (status != ZX_OK || status2 != ZX_OK) {
      DRIVER_ERROR("HeapCreateResource() failed - status: %d status2: %d", status, status2);
      // sanitize to ZX_ERR_NO_MEMORY regardless of why.
      status = ZX_ERR_NO_MEMORY;
      return status;
    }

    allocations_[parent_vmo.get()] = id;
    return ZX_OK;
  }

  void Delete(zx::vmo parent_vmo) override {
    auto it = allocations_.find(parent_vmo.get());
    if (it == allocations_.end()) {
      DRIVER_ERROR("Invalid allocation - vmo_handle: %d", parent_vmo.get());
      return;
    }
    auto id = it->second;
    zx_status_t status = fuchsia_sysmem_HeapDestroyResource(connection_.get(), id);
    if (status != ZX_OK) {
      DRIVER_ERROR("HeapDestroyResource() failed - status: %d", status);
      // fall-through - this can only fail because resource has
      // already been destroyed.
    }
    allocations_.erase(it);
    // ~parent_vmo
  }

  bool CoherencyDomainIsInaccessible() override {
    // TODO(reveman): Add support for CPU/RAM domains to external heaps.
    return true;
  }

 private:
  zx::channel connection_;
  fbl::unique_ptr<async::Wait> wait_for_close_;
  // From parent vmo handle to ID.
  std::map<zx_handle_t, uint64_t> allocations_;
};

fuchsia_sysmem_DriverConnector_ops_t driver_connector_ops = {
    .Connect = fidl::Binder<Device>::BindMember<&Device::Connect>,
    .GetProtectedMemoryInfo = fidl::Binder<Device>::BindMember<&Device::GetProtectedMemoryInfo>,
};

zx_status_t sysmem_message(void* device_ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_sysmem_DriverConnector_dispatch(device_ctx, txn, msg, &driver_connector_ops);
}

// -Werror=missing-field-initializers seems more paranoid than I want here.
zx_protocol_device_t sysmem_device_ops = [] {
  zx_protocol_device_t tmp{};
  tmp.version = DEVICE_OPS_VERSION;
  tmp.message = sysmem_message;
  return tmp;
}();

zx_status_t in_proc_sysmem_Connect(void* ctx, zx_handle_t allocator_request_param) {
  Device* self = static_cast<Device*>(ctx);
  return self->Connect(allocator_request_param);
}

zx_status_t in_proc_sysmem_RegisterHeap(void* ctx, uint64_t heap,
                                        zx_handle_t heap_connection_param) {
  Device* self = static_cast<Device*>(ctx);
  return self->RegisterHeap(heap, heap_connection_param);
}

// In-proc sysmem interface.  Essentially an in-proc version of
// fuchsia.sysmem.DriverConnector.
sysmem_protocol_ops_t in_proc_sysmem_protocol_ops = {
    .connect = in_proc_sysmem_Connect,
    .register_heap = in_proc_sysmem_RegisterHeap,
};

}  // namespace

Device::Device(zx_device_t* parent_device, Driver* parent_driver)
    : parent_device_(parent_device),
      parent_driver_(parent_driver),
      in_proc_sysmem_protocol_{.ops = &in_proc_sysmem_protocol_ops, .ctx = this} {
  ZX_DEBUG_ASSERT(parent_device_);
  ZX_DEBUG_ASSERT(parent_driver_);
}

zx_status_t Device::Bind() {
  zx_status_t status = device_get_protocol(parent_device_, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    DRIVER_ERROR("Failed device_get_protocol() ZX_PROTOCOL_PDEV - status: %d", status);
    return status;
  }

  uint64_t protected_memory_size = 0;
  uint64_t contiguous_memory_size = 0;

  sysmem_metadata_t metadata;

  size_t metadata_actual;
  status = device_get_metadata(parent_device_, SYSMEM_METADATA, &metadata, sizeof(metadata),
                               &metadata_actual);
  if (status == ZX_OK && metadata_actual == sizeof(metadata)) {
    pdev_device_info_vid_ = metadata.vid;
    pdev_device_info_pid_ = metadata.pid;
    protected_memory_size = metadata.protected_memory_size;
    contiguous_memory_size = metadata.contiguous_memory_size;
  }

  allocators_[fuchsia_sysmem_HeapType_SYSTEM_RAM] = std::make_unique<SystemRamMemoryAllocator>();

  status = pdev_get_bti(&pdev_, 0, bti_.reset_and_get_address());
  if (status != ZX_OK) {
    DRIVER_ERROR("Failed pdev_get_bti() - status: %d", status);
    return status;
  }

  zx::bti bti_copy;
  status = bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, &bti_copy);
  if (status != ZX_OK) {
    DRIVER_ERROR("BTI duplicate failed: %d", status);
    return status;
  }

  if (contiguous_memory_size) {
    auto pooled_allocator = std::make_unique<ContiguousPooledSystemRamMemoryAllocator>(
        this, "SysmemContiguousPool", contiguous_memory_size, true);
    if (pooled_allocator->Init() != ZX_OK) {
      DRIVER_ERROR("Contiguous system ram allocator initialization failed");
      return ZX_ERR_NO_MEMORY;
    }
    contiguous_system_ram_allocator_ = std::move(pooled_allocator);
  } else {
    contiguous_system_ram_allocator_ = std::make_unique<ContiguousSystemRamMemoryAllocator>(this);
  }

  // TODO: Separate protected memory allocator into separate driver or library
  if (pdev_device_info_vid_ == PDEV_VID_AMLOGIC && protected_memory_size > 0) {
    auto amlogic_allocator = std::make_unique<ContiguousPooledSystemRamMemoryAllocator>(
        this, "SysmemAmlogicProtectedPool", protected_memory_size, false);
    // Request 64kB alignment because the hardware can only modify protections along 64kB
    // boundaries.
    status = amlogic_allocator->Init(16);
    if (status != ZX_OK) {
      DRIVER_ERROR("Failed to init allocator for amlogic protected memory: %d", status);
      return status;
    }
    protected_allocator_ = amlogic_allocator.get();
    allocators_[fuchsia_sysmem_HeapType_AMLOGIC_SECURE] = std::move(amlogic_allocator);
  }

  pbus_protocol_t pbus;
  status = device_get_protocol(parent_device_, ZX_PROTOCOL_PBUS, &pbus);
  if (status != ZX_OK) {
    DRIVER_ERROR("ZX_PROTOCOL_PBUS not available %d \n", status);
    return status;
  }

  device_add_args_t device_add_args = {};
  device_add_args.version = DEVICE_ADD_ARGS_VERSION;
  device_add_args.name = "sysmem";
  device_add_args.ctx = this;
  device_add_args.ops = &sysmem_device_ops;
  device_add_args.proto_id = ZX_PROTOCOL_SYSMEM,
  device_add_args.proto_ops = &in_proc_sysmem_protocol_,

  // ZX_PROTOCOL_SYSMEM causes /dev/class/sysmem to get created, and flags
  // support for the fuchsia.sysmem.DriverConnector protocol.  The .message
  // callback used is sysmem_device_ops.message, not
  // sysmem_protocol_ops.message.
  device_add_args.proto_id = ZX_PROTOCOL_SYSMEM;
  device_add_args.proto_ops = &in_proc_sysmem_protocol_ops;
  device_add_args.flags = DEVICE_ADD_ALLOW_MULTI_COMPOSITE;

  status = device_add(parent_device_, &device_add_args, &device_);
  if (status != ZX_OK) {
    DRIVER_ERROR("Failed to bind device");
    return status;
  }

  // Register the sysmem protocol with the platform bus.
  //
  // This is essentially the in-proc version of
  // fuchsia.sysmem.DriverConnector.
  //
  // We should only pbus_register_protocol() if device_add() succeeded, but if
  // pbus_register_protocol() fails, we should remove the device without it
  // ever being visible.
  // TODO(ZX-3746) Remove this after all clients have switched to using composite protocol.
  status = pbus_register_protocol(&pbus, ZX_PROTOCOL_SYSMEM, &in_proc_sysmem_protocol_,
                                  sizeof(in_proc_sysmem_protocol_));
  if (status != ZX_OK) {
    zx_status_t remove_status = device_remove(device_);
    // If this failed, we're potentially leaving the device invisible in a
    // --release build, which is about the best we can do if removing fails.
    // Of course, remove shouldn't fail in the first place.
    ZX_DEBUG_ASSERT(remove_status == ZX_OK);
    return status;
  }

  return ZX_OK;
}

zx_status_t Device::Connect(zx_handle_t allocator_request) {
  zx::channel local_allocator_request(allocator_request);
  // The Allocator is channel-owned / self-owned.
  Allocator::CreateChannelOwned(std::move(local_allocator_request), this);
  return ZX_OK;
}

zx_status_t Device::RegisterHeap(uint64_t heap, zx_handle_t heap_connection) {
  zx::channel local_heap_connection(heap_connection);

  // External heaps should not have bit 63 set but bit 60 must be set.
  if ((heap & 0x8000000000000000) || !(heap & 0x1000000000000000)) {
    DRIVER_ERROR("Invalid external heap");
    return ZX_ERR_INVALID_ARGS;
  }

  // Clean up heap allocator after peer closed channel.
  auto wait_for_close = std::make_unique<async::Wait>(
      local_heap_connection.get(), ZX_CHANNEL_PEER_CLOSED, 0,
      async::Wait::Handler(
          [this, heap](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                       const zx_packet_signal_t* signal) { allocators_.erase(heap); }));
  // It is safe to call Begin() here before adding entry to the map as
  // handler will run on current thread.
  wait_for_close->Begin(async_get_default_dispatcher());

  // This replaces any previously registered allocator for heap. This
  // behavior is preferred as it avoids a potential race-condition during
  // heap restart.
  allocators_[heap] = std::make_unique<ExternalMemoryAllocator>(std::move(local_heap_connection),
                                                                std::move(wait_for_close));
  return ZX_OK;
}

zx_status_t Device::GetProtectedMemoryInfo(fidl_txn* txn) {
  if (!protected_allocator_) {
    return fuchsia_sysmem_DriverConnectorGetProtectedMemoryInfo_reply(txn, ZX_ERR_NOT_SUPPORTED, 0u,
                                                                      0u);
  }

  uint64_t base;
  uint64_t size;
  zx_status_t status = protected_allocator_->GetPhysicalMemoryInfo(&base, &size);
  return fuchsia_sysmem_DriverConnectorGetProtectedMemoryInfo_reply(txn, status, base, size);
}

const zx::bti& Device::bti() { return bti_; }

// TODO(ZX-4809): This shouldn't exist, but will be needed for VDEC unless we resolve ZX-4809.
zx_status_t Device::CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) {
  zx::vmo result_vmo;
  zx_status_t status =
      zx_vmo_create_physical(get_root_resource(), base, size, result_vmo.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }
  *vmo_out = std::move(result_vmo);
  return ZX_OK;
}

uint32_t Device::pdev_device_info_vid() {
  ZX_DEBUG_ASSERT(pdev_device_info_vid_ != std::numeric_limits<uint32_t>::max());
  return pdev_device_info_vid_;
}

uint32_t Device::pdev_device_info_pid() {
  ZX_DEBUG_ASSERT(pdev_device_info_pid_ != std::numeric_limits<uint32_t>::max());
  return pdev_device_info_pid_;
}

void Device::TrackToken(BufferCollectionToken* token) {
  zx_koid_t server_koid = token->server_koid();
  ZX_DEBUG_ASSERT(server_koid != ZX_KOID_INVALID);
  ZX_DEBUG_ASSERT(tokens_by_koid_.find(server_koid) == tokens_by_koid_.end());
  tokens_by_koid_.insert({server_koid, token});
}

void Device::UntrackToken(BufferCollectionToken* token) {
  zx_koid_t server_koid = token->server_koid();
  if (server_koid == ZX_KOID_INVALID) {
    // The caller is allowed to un-track a token that never saw
    // SetServerKoid().
    return;
  }
  auto iter = tokens_by_koid_.find(server_koid);
  ZX_DEBUG_ASSERT(iter != tokens_by_koid_.end());
  tokens_by_koid_.erase(iter);
}

BufferCollectionToken* Device::FindTokenByServerChannelKoid(zx_koid_t token_server_koid) {
  auto iter = tokens_by_koid_.find(token_server_koid);
  if (iter == tokens_by_koid_.end()) {
    return nullptr;
  }
  return iter->second;
}

MemoryAllocator* Device::GetAllocator(const fuchsia_sysmem_BufferMemorySettings* settings) {
  if (settings->heap == fuchsia_sysmem_HeapType_SYSTEM_RAM && settings->is_physically_contiguous) {
    return contiguous_system_ram_allocator_.get();
  }

  auto iter = allocators_.find(settings->heap);
  if (iter == allocators_.end()) {
    return nullptr;
  }
  return iter->second.get();
}
