// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <fuchsia/sysmem/llcpp/fidl.h>
#include <fuchsia/sysmem2/llcpp/fidl.h>
#include <inttypes.h>
#include <lib/async/dispatcher.h>
#include <lib/fidl-async-2/simple_binding.h>
#include <lib/fidl-utils/bind.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/assert.h>
#include <zircon/device/sysmem.h>

#include <memory>

#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/protocol/platform/bus.h>

#include "allocator.h"
#include "buffer_collection_token.h"
#include "contiguous_pooled_memory_allocator.h"
#include "driver.h"
#include "macros.h"

using sysmem_driver::MemoryAllocator;

namespace sysmem_driver {
namespace {

// Helper function to build owned HeapProperties table with coherency doman support.
llcpp::fuchsia::sysmem2::HeapProperties BuildHeapPropertiesWithCoherencyDomainSupport(
    bool cpu_supported, bool ram_supported, bool inaccessible_supported) {
  using llcpp::fuchsia::sysmem2::CoherencyDomainSupport;
  using llcpp::fuchsia::sysmem2::HeapProperties;

  auto coherency_domain_support = std::make_unique<CoherencyDomainSupport>();
  *coherency_domain_support =
      CoherencyDomainSupport::Builder(std::make_unique<CoherencyDomainSupport::Frame>())
          .set_cpu_supported(std::make_unique<bool>(cpu_supported))
          .set_ram_supported(std::make_unique<bool>(ram_supported))
          .set_inaccessible_supported(std::make_unique<bool>(inaccessible_supported))
          .build();

  return HeapProperties::Builder(std::make_unique<HeapProperties::Frame>())
      .set_coherency_domain_support(std::move(coherency_domain_support))
      .build();
}

class SystemRamMemoryAllocator : public MemoryAllocator {
 public:
  SystemRamMemoryAllocator()
      : MemoryAllocator(BuildHeapPropertiesWithCoherencyDomainSupport(true /*cpu*/, true /*ram*/,
                                                                      true /*inaccessible*/)) {}

  zx_status_t Allocate(uint64_t size, std::optional<std::string> name,
                       zx::vmo* parent_vmo) override {
    zx_status_t status = zx::vmo::create(size, 0, parent_vmo);
    if (status != ZX_OK) {
      return status;
    }
    constexpr const char vmo_name[] = "Sysmem-core";
    parent_vmo->set_property(ZX_PROP_NAME, vmo_name, sizeof(vmo_name));
    return status;
  }
  zx_status_t SetupChildVmo(const zx::vmo& parent_vmo, const zx::vmo& child_vmo) override {
    // nothing to do here
    return ZX_OK;
  }
  virtual void Delete(zx::vmo parent_vmo) override {
    // ~parent_vmo
  }
};

class ContiguousSystemRamMemoryAllocator : public MemoryAllocator {
 public:
  explicit ContiguousSystemRamMemoryAllocator(Owner* parent_device)
      : MemoryAllocator(BuildHeapPropertiesWithCoherencyDomainSupport(true /*cpu*/, true /*ram*/,
                                                                      true /*inaccessible*/)),
        parent_device_(parent_device) {}

  zx_status_t Allocate(uint64_t size, std::optional<std::string> name,
                       zx::vmo* parent_vmo) override {
    zx::vmo result_parent_vmo;
    // This code is unlikely to work after running for a while and physical
    // memory is more fragmented than early during boot. The
    // ContiguousPooledMemoryAllocator handles that case by keeping
    // a separate pool of contiguous memory.
    zx_status_t status =
        zx::vmo::create_contiguous(parent_device_->bti(), size, 0, &result_parent_vmo);
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
    constexpr const char vmo_name[] = "Sysmem-contig-core";
    result_parent_vmo.set_property(ZX_PROP_NAME, vmo_name, sizeof(vmo_name));
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

 private:
  Owner* const parent_device_;
};

class ExternalMemoryAllocator : public MemoryAllocator {
 public:
  ExternalMemoryAllocator(fidl::Client<llcpp::fuchsia::sysmem2::Heap> heap,
                          std::unique_ptr<async::Wait> wait_for_close,
                          llcpp::fuchsia::sysmem2::HeapProperties properties)
      : MemoryAllocator(std::move(properties)),
        heap_(std::move(heap)),
        wait_for_close_(std::move(wait_for_close)) {}

  zx_status_t Allocate(uint64_t size, std::optional<std::string> name,
                       zx::vmo* parent_vmo) override {
    auto result = heap_->AllocateVmo_Sync(size);
    if (!result.ok() || result.value().s != ZX_OK) {
      DRIVER_ERROR("HeapAllocate() failed - status: %d status2: %d", result.status(),
                   result.value().s);
      // sanitize to ZX_ERR_NO_MEMORY regardless of why.
      return ZX_ERR_NO_MEMORY;
    }
    zx::vmo result_vmo = std::move(result.value().vmo);
    constexpr const char vmo_name[] = "Sysmem-external-heap";
    result_vmo.set_property(ZX_PROP_NAME, vmo_name, sizeof(vmo_name));
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

    auto result = heap_->CreateResource_Sync(std::move(child_vmo_copy));
    if (!result.ok() || result.value().s != ZX_OK) {
      DRIVER_ERROR("HeapCreateResource() failed - status: %d status2: %d", result.status(),
                   result.value().s);
      // sanitize to ZX_ERR_NO_MEMORY regardless of why.
      return ZX_ERR_NO_MEMORY;
    }
    allocations_[parent_vmo.get()] = result.value().id;
    return ZX_OK;
  }

  void Delete(zx::vmo parent_vmo) override {
    auto it = allocations_.find(parent_vmo.get());
    if (it == allocations_.end()) {
      DRIVER_ERROR("Invalid allocation - vmo_handle: %d", parent_vmo.get());
      return;
    }
    auto id = it->second;
    auto result = heap_->DestroyResource_Sync(id);
    if (!result.ok()) {
      DRIVER_ERROR("HeapDestroyResource() failed - status: %d", result.status());
      // fall-through - this can only fail because resource has
      // already been destroyed.
    }
    allocations_.erase(it);
    // ~parent_vmo
  }

 private:
  fidl::Client<llcpp::fuchsia::sysmem2::Heap> heap_;
  std::unique_ptr<async::Wait> wait_for_close_;

  // From parent vmo handle to ID.
  std::map<zx_handle_t, uint64_t> allocations_;
};

fuchsia_sysmem_DriverConnector_ops_t driver_connector_ops = {
    .Connect = fidl::Binder<Device>::BindMember<&Device::Connect>,
};

}  // namespace

zx_status_t Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_sysmem_DriverConnector_dispatch(this, txn, msg, &driver_connector_ops);
}

Device::Device(zx_device_t* parent_device, Driver* parent_driver)
    : DdkDeviceType(parent_device),
      parent_driver_(parent_driver),
      loop_(&kAsyncLoopConfigNeverAttachToThread),
      in_proc_sysmem_protocol_{.ops = &sysmem_protocol_ops_, .ctx = this} {
  ZX_DEBUG_ASSERT(parent_);
  ZX_DEBUG_ASSERT(parent_driver_);
  zx_status_t status = loop_.StartThread("sysmem", &loop_thrd_);
  ZX_ASSERT(status == ZX_OK);
}

// static
void Device::OverrideSizeFromCommandLine(const char* name, uint64_t* memory_size) {
  const char* pool_arg = getenv(name);
  if (!pool_arg || strlen(pool_arg) == 0)
    return;
  char* end = nullptr;
  uint64_t override_size = strtoull(pool_arg, &end, 10);
  // Check that entire string was used and there isn't garbage at the end.
  if (*end != '\0') {
    DRIVER_ERROR("Ignoring flag %s with invalid size \"%s\"", name, pool_arg);
    return;
  }
  // Apply this alignment to contiguous pool as well, since it's small enough.
  constexpr uint64_t kMinProtectedAlignment = 64 * 1024;
  override_size = fbl::round_up(override_size, kMinProtectedAlignment);
  DRIVER_INFO("Flag %s overriding size to %ld", name, override_size);
  *memory_size = override_size;
}

void Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  // Try to ensure all tasks started before this call finish before shutting down the loop.
  async::PostTask(loop_.dispatcher(), [this]() { loop_.Quit(); });
  // JoinThreads waits for the Quit() to execute and cause the thread to exit.
  loop_.JoinThreads();
  loop_.Shutdown();
  // After this point the FIDL servers should have been shutdown and all DDK and other protocol
  // methods will error out because posting tasks to the dispatcher fails.
  txn.Reply();
}

zx_status_t Device::Bind() {
  heaps_ = inspector_.GetRoot().CreateChild("heaps");
  collections_node_ = inspector_.GetRoot().CreateChild("collections");

  zx_status_t status = ddk::PDevProtocolClient::CreateFromDevice(parent_, &pdev_);
  if (status != ZX_OK) {
    DRIVER_ERROR("Failed device_get_protocol() ZX_PROTOCOL_PDEV - status: %d", status);
    return status;
  }

  uint64_t protected_memory_size = 0;
  uint64_t contiguous_memory_size = 0;

  sysmem_metadata_t metadata;

  size_t metadata_actual;
  status = DdkGetMetadata(SYSMEM_METADATA, &metadata, sizeof(metadata), &metadata_actual);
  if (status == ZX_OK && metadata_actual == sizeof(metadata)) {
    pdev_device_info_vid_ = metadata.vid;
    pdev_device_info_pid_ = metadata.pid;
    protected_memory_size = metadata.protected_memory_size;
    contiguous_memory_size = metadata.contiguous_memory_size;
  }

  OverrideSizeFromCommandLine("driver.sysmem.protected_memory_size", &protected_memory_size);
  OverrideSizeFromCommandLine("driver.sysmem.contiguous_memory_size", &contiguous_memory_size);

  allocators_[llcpp::fuchsia::sysmem2::HeapType::SYSTEM_RAM] =
      std::make_unique<SystemRamMemoryAllocator>();

  status = pdev_.GetBti(0, &bti_);
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
    constexpr bool kIsCpuAccessible = true;
    constexpr bool kIsReady = true;
    auto pooled_allocator = std::make_unique<ContiguousPooledMemoryAllocator>(
        this, "SysmemContiguousPool", &heaps_, fuchsia_sysmem_HeapType_SYSTEM_RAM,
        contiguous_memory_size, kIsCpuAccessible, kIsReady, loop_.dispatcher());
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
    constexpr bool kIsCpuAccessible = false;
    constexpr bool kIsReady = false;
    auto amlogic_allocator = std::make_unique<ContiguousPooledMemoryAllocator>(
        this, "SysmemAmlogicProtectedPool", &heaps_, fuchsia_sysmem_HeapType_AMLOGIC_SECURE,
        protected_memory_size, kIsCpuAccessible, kIsReady, loop_.dispatcher());
    // Request 64kB alignment because the hardware can only modify protections along 64kB
    // boundaries.
    status = amlogic_allocator->Init(16);
    if (status != ZX_OK) {
      DRIVER_ERROR("Failed to init allocator for amlogic protected memory: %d", status);
      return status;
    }
    secure_allocators_[llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE] = amlogic_allocator.get();
    allocators_[llcpp::fuchsia::sysmem2::HeapType::AMLOGIC_SECURE] = std::move(amlogic_allocator);
  }

  ddk::PBusProtocolClient pbus;
  status = ddk::PBusProtocolClient::CreateFromDevice(parent_, &pbus);
  if (status != ZX_OK) {
    DRIVER_ERROR("ZX_PROTOCOL_PBUS not available %d \n", status);
    return status;
  }

  status = DdkAdd(ddk::DeviceAddArgs("sysmem")
                      .set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE)
                      .set_inspect_vmo(inspector_.DuplicateVmo()));
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
  status = pbus.RegisterProtocol(ZX_PROTOCOL_SYSMEM, &in_proc_sysmem_protocol_,
                                 sizeof(in_proc_sysmem_protocol_));
  if (status != ZX_OK) {
    DdkAsyncRemove();
    return status;
  }

  return ZX_OK;
}

zx_status_t Device::Connect(zx_handle_t allocator_request) {
  zx::channel local_allocator_request(allocator_request);
  return async::PostTask(
      loop_.dispatcher(),
      [this, local_allocator_request = std::move(local_allocator_request)]() mutable {
        // The Allocator is channel-owned / self-owned.
        Allocator::CreateChannelOwned(std::move(local_allocator_request), this);
      });
}

zx_status_t Device::SysmemConnect(zx::channel allocator_request) {
  // The Allocator is channel-owned / self-owned.
  return async::PostTask(loop_.dispatcher(),
                         [this, allocator_request = std::move(allocator_request)]() mutable {
                           // The Allocator is channel-owned / self-owned.
                           Allocator::CreateChannelOwned(std::move(allocator_request), this);
                         });
}

zx_status_t Device::SysmemRegisterHeap(uint64_t heap_param, zx::channel heap_connection) {
  // External heaps should not have bit 63 set but bit 60 must be set.
  if ((heap_param & 0x8000000000000000) || !(heap_param & 0x1000000000000000)) {
    DRIVER_ERROR("Invalid external heap");
    return ZX_ERR_INVALID_ARGS;
  }
  auto heap = static_cast<llcpp::fuchsia::sysmem2::HeapType>(heap_param);

  return async::PostTask(
      loop_.dispatcher(), [this, heap, heap_connection = std::move(heap_connection)]() mutable {
        // Clean up heap allocator after peer closed channel.
        auto wait_for_close = std::make_unique<async::Wait>(
            heap_connection.get(), ZX_CHANNEL_PEER_CLOSED, 0,
            async::Wait::Handler(
                [this, heap](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                             const zx_packet_signal_t* signal) { allocators_.erase(heap); }));
        // It is safe to call Begin() here before adding entry to the map as
        // handler will run on current thread.
        zx_status_t status = wait_for_close->Begin(dispatcher());
        if (status != ZX_OK) {
          DRIVER_ERROR("Device::RegisterHeap() failed wait_for_close->Begin()");
          return;
        }

        auto heap_client = std::make_unique<fidl::Client<llcpp::fuchsia::sysmem2::Heap>>();
        auto heap_client_ptr = heap_client.get();
        status = heap_client_ptr->Bind(
            std::move(heap_connection), loop_.dispatcher(),
            [this, heap](fidl::UnbindInfo info, zx::channel channel) {
              if (info.reason != fidl::UnbindInfo::Reason::kPeerClosed &&
                  info.reason != fidl::UnbindInfo::Reason::kClose) {
                DRIVER_ERROR("Heap failed: reason %d status %d\n", static_cast<int>(info.reason),
                             info.status);
                allocators_.erase(heap);
              }
            },
            {.on_register = [this, heap, wait_for_close = std::move(wait_for_close),
                             heap_client = std::move(heap_client)](
                                llcpp::fuchsia::sysmem2::Heap::OnRegisterResponse* message) mutable {
              // A heap should not be registered twice.
              ZX_DEBUG_ASSERT(heap_client);
              // This replaces any previously registered allocator for heap (also cancels the old
              // wait). This behavior is preferred as it avoids a potential race-condition during
              // heap restart.
              allocators_[heap] = std::make_unique<ExternalMemoryAllocator>(
                  std::move(*heap_client), std::move(wait_for_close),
                  std::move(message->properties));
            }});
        ZX_ASSERT(status == ZX_OK);
      });
}

zx_status_t Device::SysmemRegisterSecureMem(zx::channel secure_mem_connection) {
  LOG(DEBUG, "sysmem RegisterSecureMem begin");

  current_close_is_abort_ = std::make_shared<std::atomic_bool>(true);

  return async::PostTask(
      loop_.dispatcher(), [this, secure_mem_connection = std::move(secure_mem_connection),
                           close_is_abort = current_close_is_abort_]() mutable {
        // This code must run asynchronously for two reasons:
        // 1) It does synchronous IPCs to the secure mem device, so SysmemRegisterSecureMem must
        // have return so the call from the secure mem device is unblocked.
        // 2) It modifies member variables like |secure_mem_| and |heaps_| that should only be
        // touched on |loop_|'s thread.
        auto wait_for_close = std::make_unique<async::Wait>(
            secure_mem_connection.get(), ZX_CHANNEL_PEER_CLOSED, 0,
            async::Wait::Handler([this, close_is_abort](async_dispatcher_t* dispatcher,
                                                        async::Wait* wait, zx_status_t status,
                                                        const zx_packet_signal_t* signal) {
              if (*close_is_abort && secure_mem_) {
                // The server end of this channel (the aml-securemem driver) is the driver that
                // listens for suspend(mexec) so that soft reboot can succeed.  If that driver has
                // failed, intentionally force a hard reboot here to get back to a known-good state.
                //
                // TODO(dustingreen): If there's any more direct way to intentionally trigger a hard
                // reboot, that would probably be better here.
                ZX_PANIC(
                    "secure_mem_ connection unexpectedly lost; secure mem in unknown state; hard "
                    "reboot");
              }
            }));

        // It is safe to call Begin() here before setting up secure_mem_ because handler will either
        // run on current thread (loop_thrd_), or be run after the current task finishes while the
        // loop is shutting down.
        zx_status_t status = wait_for_close->Begin(dispatcher());
        if (status != ZX_OK) {
          DRIVER_ERROR("Device::RegisterSecureMem() failed wait_for_close->Begin()");
          return;
        }

        secure_mem_ = std::make_unique<SecureMemConnection>(std::move(secure_mem_connection),
                                                            std::move(wait_for_close));

        // Else we already ZX_PANIC()ed in wait_for_close.
        ZX_DEBUG_ASSERT(secure_mem_);

        // At this point secure_allocators_ has only the secure heaps that are configured via sysmem
        // (not those configured via the TEE), and the memory for these is not yet protected.  Tell
        // the TEE about these.
        ::llcpp::fuchsia::sysmem::PhysicalSecureHeaps sysmem_configured_heaps;
        for (const auto& [heap_type, allocator] : secure_allocators_) {
          uint64_t base;
          uint64_t size;
          status = allocator->GetPhysicalMemoryInfo(&base, &size);
          // Should be impossible for this to fail for now.
          ZX_ASSERT(status == ZX_OK);
          LOG(DEBUG,
              "allocator->GetPhysicalMemoryInfo() heap_type: %08lx base: %016" PRIx64
              " size: %016" PRIx64,
              static_cast<uint64_t>(heap_type), base, size);

          ::llcpp::fuchsia::sysmem::PhysicalSecureHeap& heap =
              sysmem_configured_heaps.heaps[sysmem_configured_heaps.heaps_count];
          heap.heap = static_cast<::llcpp::fuchsia::sysmem::HeapType>(heap_type);
          heap.physical_address = base;
          heap.size_bytes = size;
          ++sysmem_configured_heaps.heaps_count;
        }
        auto set_result = ::llcpp::fuchsia::sysmem::SecureMem::Call::SetPhysicalSecureHeaps(
            zx::unowned_channel(secure_mem_->channel()), std::move(sysmem_configured_heaps));
        // For now the FIDL IPC failing is fatal.  Among the reasons is without that
        // call succeeding, we haven't told the HW to secure/protect the physical
        // range. However we still allow it to fail if the secure mem device
        // unregistered itself.
        // For now it could return an error on sherlock if the bootloader is old, so
        // in that case just don't mark the allocators as ready.
        if (!set_result.ok()) {
          ZX_ASSERT(!*close_is_abort);
          return;
        }
        if (set_result->result.is_err()) {
          LOG(ERROR, "Got secure memory allocation error %d", set_result->result.err());
          return;
        }

        for (const auto& [heap_type, allocator] : secure_allocators_) {
          // The TEE has now told the HW about this heap's physical range being secure/protected.
          allocator->set_ready();
        }

        // Now we get the secure heaps that are configured via the TEE.
        auto get_result = ::llcpp::fuchsia::sysmem::SecureMem::Call::GetPhysicalSecureHeaps(
            zx::unowned_channel(secure_mem_->channel()));
        if (!get_result.ok()) {
          // For now this is fatal, since this case is very unexpected, and in this case rebooting
          // is the most plausible way to get back to a working state anyway.
          ZX_ASSERT(!*close_is_abort);
          return;
        }
        ZX_ASSERT(get_result->result.is_response());
        const ::llcpp::fuchsia::sysmem::PhysicalSecureHeaps& tee_configured_heaps =
            get_result->result.response().heaps;

        for (uint32_t heap_index = 0; heap_index < tee_configured_heaps.heaps_count; ++heap_index) {
          const ::llcpp::fuchsia::sysmem::PhysicalSecureHeap& heap =
              tee_configured_heaps.heaps[heap_index];
          constexpr bool kIsCpuAccessible = false;
          constexpr bool kIsReady = true;
          auto secure_allocator = std::make_unique<ContiguousPooledMemoryAllocator>(
              this, "tee_secure", &heaps_, static_cast<uint64_t>(heap.heap), heap.size_bytes,
              kIsCpuAccessible, kIsReady, loop_.dispatcher());
          status = secure_allocator->InitPhysical(heap.physical_address);
          // A failing status is fatal for now.
          ZX_ASSERT(status == ZX_OK);
          LOG(DEBUG,
              "created secure allocator: heap_type: %08lx base: %016" PRIx64 " size: %016" PRIx64,
              static_cast<uint64_t>(heap.heap), heap.physical_address, heap.size_bytes);
          auto heap_type = static_cast<llcpp::fuchsia::sysmem2::HeapType>(heap.heap);
          ZX_ASSERT(secure_allocators_.find(heap_type) == secure_allocators_.end());
          secure_allocators_[heap_type] = secure_allocator.get();
          ZX_ASSERT(allocators_.find(heap_type) == allocators_.end());
          allocators_[heap_type] = std::move(secure_allocator);
        }

        LOG(DEBUG, "sysmem RegisterSecureMem() done (async)");
      });
}

// This call allows us to tell the difference between expected vs. unexpected close of the tee_
// channel.
zx_status_t Device::SysmemUnregisterSecureMem() {
  // By this point, the aml-securemem driver's suspend(mexec) has already prepared for mexec.
  //
  // In this path, the server end of the channel hasn't closed yet, but will be closed shortly after
  // return from UnregisterSecureMem().
  //
  // We set a flag here so that a PEER_CLOSED of the channel won't cause the wait handler to crash.
  *current_close_is_abort_ = false;
  current_close_is_abort_.reset();
  return async::PostTask(loop_.dispatcher(), [this]() {
    LOG(DEBUG, "begin UnregisterSecureMem()");
    secure_mem_.reset();
    LOG(DEBUG, "end UnregisterSecureMem()");
  });
}

const zx::bti& Device::bti() { return bti_; }

// Only use this in cases where we really can't use zx::vmo::create_contiguous() because we must
// specify a specific physical range.
zx_status_t Device::CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) {
  zx::vmo result_vmo;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_resource(get_root_resource());
  zx_status_t status = zx::vmo::create_physical(*root_resource, base, size, &result_vmo);
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

MemoryAllocator* Device::GetAllocator(
    const llcpp::fuchsia::sysmem2::BufferMemorySettings::Builder& settings) {
  if (settings.heap() == llcpp::fuchsia::sysmem2::HeapType::SYSTEM_RAM &&
      settings.is_physically_contiguous()) {
    return contiguous_system_ram_allocator_.get();
  }

  auto iter = allocators_.find(settings.heap());
  if (iter == allocators_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

const llcpp::fuchsia::sysmem2::HeapProperties& Device::GetHeapProperties(
    llcpp::fuchsia::sysmem2::HeapType heap) const {
  ZX_DEBUG_ASSERT(allocators_.find(heap) != allocators_.end());
  return allocators_.at(heap)->heap_properties();
}

Device::SecureMemConnection::SecureMemConnection(zx::channel connection,
                                                 std::unique_ptr<async::Wait> wait_for_close)
    : connection_(std::move(connection)), wait_for_close_(std::move(wait_for_close)) {
  // nothing else to do here
}

zx_handle_t Device::SecureMemConnection::channel() {
  ZX_DEBUG_ASSERT(connection_);
  return connection_.get();
}

}  // namespace sysmem_driver
