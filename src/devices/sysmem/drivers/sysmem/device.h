// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_DEVICE_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_DEVICE_H_

#include <fidl/fuchsia.sysmem/cpp/wire.h>
#include <fidl/fuchsia.sysmem2/cpp/wire.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <fuchsia/hardware/sysmem/c/banjo.h>
#include <fuchsia/hardware/sysmem/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fit/thread_checker.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>

#include <limits>
#include <map>
#include <memory>
#include <unordered_set>

#include <ddktl/device.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

#include "memory_allocator.h"
#include "sysmem_metrics.h"
#include "table_set.h"

namespace sys {
class ServiceDirectory;
}  // namespace sys

namespace sysmem_driver {

class Device;
using DdkDeviceType =
    ddk::Device<Device, ddk::Messageable<fuchsia_sysmem::DriverConnector>::Mixin, ddk::Unbindable>;

class Driver;
class BufferCollectionToken;
class LogicalBufferCollection;

struct Settings {
  // Maximum size of a single allocation. Mainly useful for unit tests.
  uint64_t max_allocation_size = UINT64_MAX;
};

class Device final : public DdkDeviceType,
                     public ddk::SysmemProtocol<Device, ddk::base_protocol>,
                     public MemoryAllocator::Owner {
 public:
  Device(zx_device_t* parent_device, Driver* parent_driver);

  [[nodiscard]] zx_status_t OverrideSizeFromCommandLine(const char* name, int64_t* memory_size);
  [[nodiscard]] zx_status_t GetContiguousGuardParameters(
      uint64_t* guard_bytes_out, bool* unused_pages_guarded,
      zx::duration* unused_page_check_cycle_period, bool* internal_guard_pages_out,
      bool* crash_on_fail_out);

  [[nodiscard]] zx_status_t Bind();

  //
  // The rest of the methods are only valid to call after Bind().
  //

  // SysmemProtocol implementation.
  [[nodiscard]] zx_status_t SysmemConnect(zx::channel allocator_request);
  [[nodiscard]] zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection);
  [[nodiscard]] zx_status_t SysmemRegisterSecureMem(zx::channel tee_connection);
  [[nodiscard]] zx_status_t SysmemUnregisterSecureMem();

  // Ddk mixin implementations.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease() { delete this; }

  // MemoryAllocator::Owner implementation.
  [[nodiscard]] const zx::bti& bti() override;
  [[nodiscard]] zx_status_t CreatePhysicalVmo(uint64_t base, uint64_t size,
                                              zx::vmo* vmo_out) override;
  void CheckForUnbind() override;
  TableSet& table_set() override;
  SysmemMetrics& metrics() override;

  inspect::Node* heap_node() override { return &heaps_; }

  void Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) override;
  void SetAuxServiceDirectory(SetAuxServiceDirectoryRequestView request,
                              SetAuxServiceDirectoryCompleter::Sync& completer) override;

  [[nodiscard]] uint32_t pdev_device_info_vid();

  [[nodiscard]] uint32_t pdev_device_info_pid();

  // Track/untrack the token by the koid of the server end of its FIDL
  // channel.  TrackToken() is only allowed after token->SerServerKoid().
  // UntrackToken() is allowed even if there was never a
  // token->SetServerKoid() (in which case it's a nop).
  //
  // While tracked, a token can be found with FindTokenByServerChannelKoid().
  void TrackToken(BufferCollectionToken* token);
  void UntrackToken(BufferCollectionToken* token);

  // Finds and removes token_server_koid from unfound_token_koids_.
  [[nodiscard]] bool TryRemoveKoidFromUnfoundTokenList(zx_koid_t token_server_koid);

  // Find the BufferCollectionToken (if any) by the koid of the server end of
  // its FIDL channel.
  [[nodiscard]] BufferCollectionToken* FindTokenByServerChannelKoid(zx_koid_t token_server_koid);

  // Get allocator for |settings|. Returns NULL if allocator is not
  // registered for settings.
  [[nodiscard]] MemoryAllocator* GetAllocator(
      const fuchsia_sysmem2::wire::BufferMemorySettings& settings);

  // Get heap properties of a specific memory heap allocator.
  //
  // Clients should guarantee that the heap is valid and already registered
  // to sysmem driver.
  [[nodiscard]] const fuchsia_sysmem2::wire::HeapProperties& GetHeapProperties(
      fuchsia_sysmem2::wire::HeapType heap) const;

  [[nodiscard]] const sysmem_protocol_t* proto() const { return &in_proc_sysmem_protocol_; }
  [[nodiscard]] const zx_device_t* device() const { return zxdev_; }
  [[nodiscard]] async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

  // Test hook
  [[nodiscard]] std::unordered_set<LogicalBufferCollection*>& logical_buffer_collections() {
    std::lock_guard checker(*loop_checker_);
    return logical_buffer_collections_;
  }

  // Test hook
  void AddLogicalBufferCollection(LogicalBufferCollection* collection) {
    std::lock_guard checker(*loop_checker_);
    logical_buffer_collections_.insert(collection);
  }

  // Test hook
  void RemoveLogicalBufferCollection(LogicalBufferCollection* collection) {
    std::lock_guard checker(*loop_checker_);
    logical_buffer_collections_.erase(collection);
    CheckForUnbind();
  }

  [[nodiscard]] inspect::Node& collections_node() { return collections_node_; }

  void set_settings(const Settings& settings) { settings_ = settings; }

  [[nodiscard]] const Settings& settings() const { return settings_; }

  void ResetThreadCheckerForTesting() { loop_checker_.emplace(fit::thread_checker()); }

 private:
  class SecureMemConnection {
   public:
    SecureMemConnection(zx::channel connection, std::unique_ptr<async::Wait> wait_for_close);
    zx_handle_t channel();

   private:
    zx::channel connection_;
    std::unique_ptr<async::Wait> wait_for_close_;
  };

  Driver* parent_driver_ = nullptr;
  inspect::Inspector inspector_;
  async::Loop loop_;
  thrd_t loop_thrd_;
  // During initialization this checks that operations are performed on a DDK thread. After
  // initialization, it checks that operations are on the loop thread.
  mutable std::optional<fit::thread_checker> loop_checker_;

  TableSet table_set_;

  // Currently located at bootstrap/driver_manager:root/sysmem.
  inspect::Node sysmem_root_;
  inspect::Node heaps_;

  inspect::Node collections_node_;

  ddk::PDevProtocolClient pdev_;
  zx::bti bti_;

  // Initialize these to a value that won't be mistaken for a real vid or pid.
  uint32_t pdev_device_info_vid_ = std::numeric_limits<uint32_t>::max();
  uint32_t pdev_device_info_pid_ = std::numeric_limits<uint32_t>::max();

  // In-proc sysmem interface.  Essentially an in-proc version of
  // fuchsia.sysmem.DriverConnector.
  sysmem_protocol_t in_proc_sysmem_protocol_;

  // This map allows us to look up the BufferCollectionToken by the koid of
  // the server end of a BufferCollectionToken channel.
  std::map<zx_koid_t, BufferCollectionToken*> tokens_by_koid_ __TA_GUARDED(*loop_checker_);

  std::deque<zx_koid_t> unfound_token_koids_ __TA_GUARDED(*loop_checker_);

  // This map contains all registered memory allocators.
  std::map<fuchsia_sysmem2::wire::HeapType, std::shared_ptr<MemoryAllocator>> allocators_
      __TA_GUARDED(*loop_checker_);

  // Some memory allocators need to be registered with properties before
  // we can use them to allocate memory. We keep this map to store all the
  // unregistered allocators.
  std::map<MemoryAllocator*,
           std::pair<fuchsia_sysmem2::wire::HeapType, std::unique_ptr<MemoryAllocator>>>
      unregistered_allocators_ __TA_GUARDED(*loop_checker_);

  // This map contains only the secure allocators, if any.  The pointers are owned by allocators_.
  //
  // TODO(dustingreen): Consider unordered_map for this and some of above.
  std::map<fuchsia_sysmem2::wire::HeapType, MemoryAllocator*> secure_allocators_
      __TA_GUARDED(*loop_checker_);

  // This flag is used to determine if the closing of the current secure mem
  // connection is an error (true), or expected (false).
  std::shared_ptr<std::atomic_bool> current_close_is_abort_;

  // This has the connection to the securemem driver, if any.  Once allocated this is supposed to
  // stay allocated unless mexec is about to happen.  The server end takes care of handling
  // DdkSuspend() to allow mexec to work.  For example, by calling secmem TA.  This channel will
  // close from the server end when DdkSuspend(mexec) happens, but only after
  // UnregisterSecureMem().
  std::unique_ptr<SecureMemConnection> secure_mem_ __TA_GUARDED(*loop_checker_);

  std::unique_ptr<MemoryAllocator> contiguous_system_ram_allocator_ __TA_GUARDED(*loop_checker_);

  std::unordered_set<LogicalBufferCollection*> logical_buffer_collections_
      __TA_GUARDED(*loop_checker_);

  Settings settings_;

  bool waiting_for_unbind_ __TA_GUARDED(*loop_checker_) = false;

  SysmemMetrics metrics_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_DEVICE_H_
