// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_DEVICE_H_
#define SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_DEVICE_H_

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/closure-queue/closure_queue.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>

#include <limits>
#include <map>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/sysmem.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <fbl/vector.h>
#include <region-alloc/region-alloc.h>

#include "memory_allocator.h"

namespace sysmem_driver {

class Device;
using DdkDeviceType = ddk::Device<Device, ddk::Messageable, ddk::UnbindableNew>;

class Driver;
class BufferCollectionToken;

class Device final : public DdkDeviceType,
                     public ddk::SysmemProtocol<Device, ddk::base_protocol>,
                     public MemoryAllocator::Owner {
 public:
  Device(zx_device_t* parent_device, Driver* parent_driver);

  static void OverrideSizeFromCommandLine(const char* name, uint64_t* memory_size);

  zx_status_t Bind();

  //
  // The rest of the methods are only valid to call after Bind().
  //

  // SysmemProtocol implementation.
  zx_status_t SysmemConnect(zx::channel allocator_request);
  zx_status_t SysmemRegisterHeap(uint64_t heap, zx::channel heap_connection);
  zx_status_t SysmemRegisterSecureMem(zx::channel tee_connection);
  zx_status_t SysmemUnregisterSecureMem();

  // Ddk mixin implementations.
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease() {
    // Don't do anything. The sysmem driver assumes it's alive for the
    // lifetime of the system.
  }

  // MemoryAllocator::Owner implementation.
  const zx::bti& bti() override;

  zx_status_t Connect(zx_handle_t allocator_request);

  zx_status_t CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) override;

  uint32_t pdev_device_info_vid();

  uint32_t pdev_device_info_pid();

  // Track/untrack the token by the koid of the server end of its FIDL
  // channel.  TrackToken() is only allowed after token->SerServerKoid().
  // UntrackToken() is allowed even if there was never a
  // token->SetServerKoid() (in which case it's a nop).
  //
  // While tracked, a token can be found with FindTokenByServerChannelKoid().
  void TrackToken(BufferCollectionToken* token);
  void UntrackToken(BufferCollectionToken* token);

  // Find the BufferCollectionToken (if any) by the koid of the server end of
  // its FIDL channel.
  BufferCollectionToken* FindTokenByServerChannelKoid(zx_koid_t token_server_koid);

  // Get allocator for |settings|. Returns NULL if allocator is not
  // registered for settings.
  MemoryAllocator* GetAllocator(const fuchsia_sysmem_BufferMemorySettings* settings);

  const sysmem_protocol_t* proto() const { return &in_proc_sysmem_protocol_; }
  const zx_device_t* device() const { return zxdev_; }
  async_dispatcher_t* dispatcher() { return loop_.dispatcher(); }

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
  async::Loop loop_;
  thrd_t loop_thrd_;

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
  std::map<zx_koid_t, BufferCollectionToken*> tokens_by_koid_;

  // This map contains all registered memory allocators.
  std::map<fuchsia_sysmem_HeapType, std::unique_ptr<MemoryAllocator>> allocators_;

  // This map contains only the secure allocators, if any.  The pointers are owned by allocators_.
  //
  // TODO(dustingreen): Consider unordered_map for this and some of above.
  std::map<fuchsia_sysmem_HeapType, MemoryAllocator*> secure_allocators_;

  // This flag is used to determine if the closing of the current secure mem
  // connection is an error (true), or expected (false).
  std::shared_ptr<std::atomic_bool> current_close_is_abort_;

  // This has the connection to the securemem driver, if any.  Once allocated this is supposed to
  // stay allocated unless mexec is about to happen.  The server end takes care of handling
  // DdkSuspendNew() to allow mexec to work.  For example, by calling secmem TA.  This channel will
  // close from the server end when DdkSuspendNew(mexec) happens, but only after
  // UnregisterSecureMem().
  std::unique_ptr<SecureMemConnection> secure_mem_;

  std::unique_ptr<MemoryAllocator> contiguous_system_ram_allocator_;
};

}  // namespace sysmem_driver

#endif  // SRC_DEVICES_SYSMEM_DRIVERS_SYSMEM_DEVICE_H_
