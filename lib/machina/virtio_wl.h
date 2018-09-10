// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_WL_H_
#define GARNET_LIB_MACHINA_VIRTIO_WL_H_

#include <unordered_map>

#include <lib/async/cpp/wait.h>
#include <virtio/virtio_ids.h>
#include <virtio/wl.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/machina/virtio_device.h"

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
  VirtioWl(const PhysMem& phys_mem, zx::vmar vmar,
           async_dispatcher_t* dispatcher);
  ~VirtioWl() override;

  VirtioQueue* in_queue() { return queue(VIRTWL_VQ_IN); }
  VirtioQueue* out_queue() { return queue(VIRTWL_VQ_OUT); }

  zx::vmar* vmar() { return &vmar_; }

  // Begins processing any descriptors that become available in the queues.
  zx_status_t Init();

 private:
  class Vfd;

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

  // Allocate memory of |size| that is shared between guest and host.
  // Returns a valid Vfd instance on success. |guest_addr| will be
  // page-aligned and non-zero on success. |size| will be rounded up to
  // the next page size boundary and returned in |actual_size|.
  std::unique_ptr<Vfd> AllocateMemory(uint32_t size, zx_gpaddr_t* guest_addr,
                                      uint64_t* actual_size);

  zx::vmar vmar_;
  async_dispatcher_t* dispatcher_;
  async::Wait in_queue_wait_;
  async::Wait out_queue_wait_;
  std::unordered_map<uint32_t, std::unique_ptr<Vfd>> vfds_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_WL_H_
