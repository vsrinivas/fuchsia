// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_CONTROL_DEVICE_H_
#define SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_CONTROL_DEVICE_H_

#include <fuchsia/hardware/goldfish/addressspace/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/control/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <fuchsia/hardware/goldfish/sync/cpp/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/io-buffer.h>
#include <lib/fpromise/result.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <zircon/types.h>

#include <map>
#include <vector>

#include <ddktl/device.h>
#include <fbl/condition_variable.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>

#include "src/graphics/drivers/misc/goldfish_control/heap.h"

namespace goldfish {

class Control;
using ControlType =
    ddk::Device<Control, ddk::Messageable<fuchsia_hardware_goldfish::ControlDevice>::Mixin,
                ddk::GetProtocolable>;

class Control : public ControlType,
                public ddk::GoldfishControlProtocol<Control, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  explicit Control(zx_device_t* parent);
  ~Control();

  zx_status_t Bind();

  uint64_t RegisterBufferHandle(const zx::vmo& vmo);
  void FreeBufferHandle(uint64_t id);

  using CreateColorBuffer2Result = fpromise::result<
      fidl::WireResponse<fuchsia_hardware_goldfish::ControlDevice::CreateColorBuffer2>,
      zx_status_t>;

  CreateColorBuffer2Result CreateColorBuffer2(
      zx::vmo vmo, fuchsia_hardware_goldfish::wire::CreateColorBuffer2Params create_params);

  // |fidl::WireServer<fuchsia_hardware_goldfish::ControlDevice>|
  void CreateColorBuffer2(CreateColorBuffer2RequestView request,
                          CreateColorBuffer2Completer::Sync& completer) override;

  using CreateBuffer2Result =
      fpromise::result<fuchsia_hardware_goldfish::wire::ControlDeviceCreateBuffer2Result,
                       zx_status_t>;

  CreateBuffer2Result CreateBuffer2(
      fidl::AnyArena& allocator, zx::vmo vmo,
      fuchsia_hardware_goldfish::wire::CreateBuffer2Params create_params);

  // |fidl::WireServer<fuchsia_hardware_goldfish::ControlDevice>|
  void CreateBuffer2(CreateBuffer2RequestView request,
                     CreateBuffer2Completer::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::ControlDevice>|
  void CreateSyncFence(CreateSyncFenceRequestView request,
                       CreateSyncFenceCompleter::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::ControlDevice>|
  void GetBufferHandle(GetBufferHandleRequestView request,
                       GetBufferHandleCompleter::Sync& completer) override;

  // |fidl::WireServer<fuchsia_hardware_goldfish::ControlDevice>|
  void GetBufferHandleInfo(GetBufferHandleInfoRequestView request,
                           GetBufferHandleInfoCompleter::Sync& completer) override;

  // Device protocol implementation.
  void DdkRelease();
  zx_status_t DdkGetProtocol(uint32_t proto_id, void* out_protocol);
  zx_status_t GoldfishControlGetColorBuffer(zx::vmo vmo, uint32_t* out_id);
  zx_status_t GoldfishControlCreateSyncFence(zx::eventpair event);

  // Used by heaps. Removes a specific heap from the linked list.
  void RemoveHeap(Heap* heap);

  fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceChildDriver>* address_space_child()
      const {
    return address_space_child_.get();
  }

 private:
  zx_status_t Init();

  zx_status_t InitAddressSpaceDeviceLocked() TA_REQ(lock_);
  zx_status_t InitPipeDeviceLocked() TA_REQ(lock_);
  zx_status_t InitSyncDeviceLocked() TA_REQ(lock_);

  // Create a pair of channel and register a sysmem Heap of |heap_type| using
  // the channel pair. The client-side channel is sent to sysmem, and the
  // server-side channel is bound to |heap|.
  zx_status_t RegisterAndBindHeap(fuchsia_sysmem2::wire::HeapType heap_type, Heap* heap);

  // TODO(fxbug.dev/81211): Remove these pipe IO functions and use
  // //src/devices/lib/goldfish/pipe_io instead.
  int32_t WriteLocked(uint32_t cmd_size, int32_t* consumed_size) TA_REQ(lock_);
  void WriteLocked(uint32_t cmd_size) TA_REQ(lock_);
  zx_status_t ReadResultLocked(void* result, size_t size) TA_REQ(lock_);
  zx_status_t ReadResultLocked(uint32_t* result) TA_REQ(lock_) {
    return ReadResultLocked(result, sizeof(uint32_t));
  }
  zx_status_t ExecuteCommandLocked(uint32_t cmd_size, uint32_t* result) TA_REQ(lock_);
  zx_status_t CreateBuffer2Locked(uint64_t size, uint32_t memory_property, uint32_t* id)
      TA_REQ(lock_);
  zx_status_t CreateColorBufferLocked(uint32_t width, uint32_t height, uint32_t format,
                                      uint32_t* id) TA_REQ(lock_);
  void CloseBufferOrColorBufferLocked(uint32_t id) TA_REQ(lock_);
  void CloseBufferLocked(uint32_t id) TA_REQ(lock_);
  void CloseColorBufferLocked(uint32_t id) TA_REQ(lock_);
  zx_status_t SetColorBufferVulkanModeLocked(uint32_t id, uint32_t mode, uint32_t* result)
      TA_REQ(lock_);
  zx_status_t SetColorBufferVulkanMode2Locked(uint32_t id, uint32_t mode, uint32_t memory_property,
                                              uint32_t* result) TA_REQ(lock_);
  zx_status_t MapGpaToBufferHandleLocked(uint32_t id, uint64_t gpa, uint64_t size, uint32_t* result)
      TA_REQ(lock_);
  zx_status_t CreateSyncKHRLocked(uint64_t* glsync_out, uint64_t* syncthread_out) TA_REQ(lock_);

  fbl::Mutex lock_;
  ddk::GoldfishPipeProtocolClient pipe_;
  ddk::GoldfishControlProtocolClient control_;
  ddk::GoldfishAddressSpaceProtocolClient address_space_;
  ddk::GoldfishSyncProtocolClient sync_;
  int32_t id_ = 0;
  zx::bti bti_ TA_GUARDED(lock_);
  ddk::IoBuffer cmd_buffer_ TA_GUARDED(lock_);
  ddk::IoBuffer io_buffer_ TA_GUARDED(lock_);

  fbl::DoublyLinkedList<std::unique_ptr<Heap>> heaps_ TA_GUARDED(lock_);
  std::vector<std::unique_ptr<Heap>> removed_heaps_;

  zx::event pipe_event_;

  std::unique_ptr<fidl::WireSyncClient<fuchsia_hardware_goldfish::AddressSpaceChildDriver>>
      address_space_child_;
  std::unique_ptr<fidl::WireSyncClient<fuchsia_hardware_goldfish::SyncTimeline>> sync_timeline_;

  // TODO(fxbug.dev/3213): This should be std::unordered_map.
  std::map<zx_koid_t, uint32_t> buffer_handles_ TA_GUARDED(lock_);

  struct BufferHandleInfo {
    fuchsia_hardware_goldfish::wire::BufferHandleType type;
    uint32_t memory_property;
  };
  std::map<uint32_t, BufferHandleInfo> buffer_handle_info_ TA_GUARDED(lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(Control);
};

}  // namespace goldfish

#endif  // SRC_GRAPHICS_DRIVERS_MISC_GOLDFISH_CONTROL_CONTROL_DEVICE_H_
