// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_WL_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_WL_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <virtio/virtio_ids.h>
#include <virtio/wl.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <deque>
#include <unordered_map>

#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/virtio_magma.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

#define VIRTWL_VQ_IN 0
#define VIRTWL_VQ_OUT 1
#define VIRTWL_VQ_MAGMA_IN 2
#define VIRTWL_VQ_MAGMA_OUT 3
#define VIRTWL_QUEUE_COUNT 4
#define VIRTWL_NEXT_VFD_ID_BASE (1 << 30)
#define VIRTWL_VFD_ID_HOST_MASK VIRTWL_NEXT_VFD_ID_BASE

// Virtio wayland device.
class VirtioWl : public DeviceBase<VirtioWl>,
                 public fuchsia::virtualization::hardware::VirtioWayland {
 public:
  class Vfd {
   public:
    Vfd() = default;
    virtual ~Vfd() = default;

    // Begin waiting on data to read from VFD. Returns ZX_ERR_NOT_SUPPORTED
    // if VFD type doesn't support reading.
    virtual zx_status_t BeginWaitOnData() { return ZX_ERR_NOT_SUPPORTED; }

    // Returns the number of |bytes| and |handles| that are available for
    // reading.
    //
    // Returns ZX_ERR_NOT_SUPPORTED if reading is not supported.
    virtual zx_status_t AvailableForRead(uint32_t* bytes, uint32_t* handles) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Read at most |num_bytes| into |bytes| and at most |num_handles|
    // into |handles|. Returns actual bytes read in |actual_bytes| and
    // actual handles read in |actual_handles|.
    //
    // Returns ZX_ERR_NOT_SUPPORTED if reading is not supported.
    virtual zx_status_t Read(void* bytes, zx_handle_info_t* handles,
                             uint32_t num_bytes, uint32_t num_handles,
                             uint32_t* actual_bytes, uint32_t* actual_handles) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Begin waiting on VFD to become ready for writing. Returns
    // ZX_ERR_NOT_SUPPORTED if VFD type doesn't support writing.
    virtual zx_status_t BeginWaitOnWritable() { return ZX_ERR_NOT_SUPPORTED; }

    // Write |bytes| and |handles| to local end-point of VFD. If VFD has
    // insufficient space for |bytes|, it writes nothing and returns
    // ZX_ERR_SHOULD_WAIT.
    //
    // Returns ZX_ERR_NOT_SUPPORTED if writing is not supported.
    virtual zx_status_t Write(const void* bytes, uint32_t num_bytes,
                              const zx_handle_t* handles, uint32_t num_handles,
                              size_t* actual_bytes) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Duplicate object for passing to channel.
    //
    // Returns ZX_ERR_NOT_SUPPORTED if duplication is not supported.
    virtual zx_status_t Duplicate(zx::handle* handle) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  };

  VirtioWl(component::StartupContext* context);
  ~VirtioWl() override = default;
  zx_status_t OnDeviceReady(uint32_t negotiated_features);

  VirtioQueue* in_queue() { return &queues_[VIRTWL_VQ_IN]; }
  VirtioQueue* out_queue() { return &queues_[VIRTWL_VQ_OUT]; }
  VirtioQueue* magma_in_queue() { return &queues_[VIRTWL_VQ_MAGMA_IN]; }
  VirtioQueue* magma_out_queue() { return &queues_[VIRTWL_VQ_MAGMA_OUT]; }

  zx::vmar* vmar() { return &vmar_; }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override;
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                      zx_gpaddr_t avail, zx_gpaddr_t used,
                      ConfigureQueueCallback callback) override;
  void NotifyQueue(uint16_t queue) override;

  // |fuchsia::virtualization::hardware::VirtioWayland|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info,
             zx::vmar vmar,
             fidl::InterfaceHandle<fuchsia::virtualization::WaylandDispatcher>
                 dispatcher,
             StartCallback callback) override;

 private:
  void HandleCommand(VirtioChain* chain);
  void HandleNew(const virtio_wl_ctrl_vfd_new_t* request,
                 virtio_wl_ctrl_vfd_new_t* response);
  void HandleClose(const virtio_wl_ctrl_vfd_t* request,
                   virtio_wl_ctrl_hdr_t* response);
  zx_status_t HandleSend(const virtio_wl_ctrl_vfd_send_t* request,
                         uint32_t request_len, virtio_wl_ctrl_hdr_t* response);
  void HandleNewCtx(const virtio_wl_ctrl_vfd_new_t* request,
                    virtio_wl_ctrl_vfd_new_t* response);
  void HandleNewPipe(const virtio_wl_ctrl_vfd_new_t* request,
                     virtio_wl_ctrl_vfd_new_t* response);
  void HandleNewDmabuf(const virtio_wl_ctrl_vfd_new_t* request,
                       virtio_wl_ctrl_vfd_new_t* response);
  void HandleDmabufSync(const virtio_wl_ctrl_vfd_dmabuf_sync_t* request,
                        virtio_wl_ctrl_hdr_t* response);

  void OnCommandAvailable();
  void OnDataAvailable(uint32_t vfd_id, async::Wait* wait, zx_status_t status,
                       const zx_packet_signal_t* signal);
  void OnCanWrite(async_dispatcher_t* dispatcher, async::Wait* wait,
                  zx_status_t status, const zx_packet_signal_t* signal);
  void DispatchPendingEvents();
  bool AcquireWritableDescriptor(VirtioQueue* queue, VirtioChain* chain,
                                 VirtioDescriptor* desc);
  bool CreatePendingVfds();

  std::array<VirtioQueue, VIRTWL_QUEUE_COUNT> queues_;
  zx::vmar vmar_;
  fuchsia::virtualization::WaylandDispatcherPtr dispatcher_;
  VirtioChain out_chain_;
  size_t bytes_written_for_send_request_ = 0;
  std::unordered_map<uint32_t, std::unique_ptr<Vfd>> vfds_;
  std::unordered_map<uint32_t, zx_signals_t> ready_vfds_;
  uint32_t next_vfd_id_ = VIRTWL_NEXT_VFD_ID_BASE;
  std::unique_ptr<VirtioMagma> magma_;

  // A pending VFD is a zircon handle that will will be converted into a VFD
  // by sending a NEW_VFD command back to the guest.
  struct PendingVfd {
    // The handle to turn into a VFD.
    zx_handle_info handle_info;

    // The VFD id that has been assigned. This must be honored as it will have
    // already been written into the command payload associated for the
    // associated RECV message.
    uint32_t vfd_id;

    // If this is valid, it will be returned _after_ the new VFD command for
    // this handle is sent.
    //
    // This will only be set on the VFD associated with last handle sent with
    // a RECV command. This is because the VFD ids in that RECV command will
    // not be valid until _all_ the VFDs are created.
    VirtioChain payload;
  };
  std::deque<PendingVfd> pending_vfds_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_WL_H_
