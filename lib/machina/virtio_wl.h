// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_WL_H_
#define GARNET_LIB_MACHINA_VIRTIO_WL_H_

#include <unordered_map>

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <virtio/virtio_ids.h>
#include <virtio/wl.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio_device.h"
#include "garnet/lib/machina/virtio_queue_waiter.h"

#define VIRTWL_VQ_IN 0
#define VIRTWL_VQ_OUT 1
#define VIRTWL_QUEUE_COUNT 2
#define VIRTWL_NEXT_VFD_ID_BASE (1 << 31)
#define VIRTWL_VFD_ID_HOST_MASK VIRTWL_NEXT_VFD_ID_BASE

namespace machina {

// Virtio wayland device.
class VirtioWl : public VirtioInprocessDevice<VIRTIO_ID_WL, VIRTWL_QUEUE_COUNT,
                                              virtio_wl_config_t> {
 public:
  class Vfd {
   public:
    Vfd() = default;
    virtual ~Vfd() = default;

    // Begin waiting on VFD to become ready. Returns ZX_ERR_NOT_SUPPORTED
    // if VFD type doesn't support waiting.
    virtual zx_status_t BeginWait(async_dispatcher_t* dispatcher) {
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

    // Write |bytes| and |handles| to local end-point of VFD.
    //
    // Returns ZX_ERR_NOT_SUPPORTED if writing is not supported.
    virtual zx_status_t Write(const void* bytes, uint32_t num_bytes,
                              const zx_handle_t* handles,
                              uint32_t num_handles) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    // Duplicate object for dispatcher.
    //
    // Returns ZX_ERR_NOT_SUPPORTED if duplication is not supported.
    virtual zx_status_t Duplicate(zx::handle* handle) {
      return ZX_ERR_NOT_SUPPORTED;
    }
  };
  using OnNewConnectionCallback = fit::function<void(zx::channel)>;

  VirtioWl(const PhysMem& phys_mem, zx::vmar vmar,
           async_dispatcher_t* dispatcher,
           OnNewConnectionCallback on_new_connection_callback);
  ~VirtioWl() override = default;

  VirtioQueue* in_queue() { return queue(VIRTWL_VQ_IN); }
  VirtioQueue* out_queue() { return queue(VIRTWL_VQ_OUT); }

  zx::vmar* vmar() { return &vmar_; }

  // Begins processing any descriptors that become available in the queues.
  zx_status_t Init();

 private:
  zx_status_t HandleCommand(VirtioQueue* queue, uint16_t head, uint32_t* used);
  void HandleNew(const virtio_wl_ctrl_vfd_new_t* request,
                 virtio_wl_ctrl_vfd_new_t* response);
  void HandleClose(const virtio_wl_ctrl_vfd_t* request,
                   virtio_wl_ctrl_hdr_t* response);
  void HandleSend(const virtio_wl_ctrl_vfd_send_t* request,
                  uint32_t request_len, virtio_wl_ctrl_hdr_t* response);
  void HandleNewCtx(const virtio_wl_ctrl_vfd_new_t* request,
                    virtio_wl_ctrl_vfd_new_t* response);
  void HandleNewPipe(const virtio_wl_ctrl_vfd_new_t* request,
                     virtio_wl_ctrl_vfd_new_t* response);
  void HandleNewDmabuf(const virtio_wl_ctrl_vfd_new_t* request,
                       virtio_wl_ctrl_vfd_new_t* response);
  void HandleDmabufSync(const virtio_wl_ctrl_vfd_dmabuf_sync_t* request,
                        virtio_wl_ctrl_hdr_t* response);

  void OnReady(uint32_t vfd_id, async::Wait* wait, zx_status_t status,
               const zx_packet_signal_t* signal);
  void BeginWaitOnQueue();
  void OnQueueReady(zx_status_t status, uint16_t index);

  zx::vmar vmar_;
  async_dispatcher_t* const dispatcher_;
  OnNewConnectionCallback on_new_connection_callback_;
  async::Wait out_queue_wait_;
  VirtioQueueWaiter in_queue_wait_;
  std::unordered_map<uint32_t, std::unique_ptr<Vfd>> vfds_;
  std::unordered_map<uint32_t, zx_signals_t> ready_vfds_;
  uint32_t next_vfd_id_ = VIRTWL_NEXT_VFD_ID_BASE;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_WL_H_
