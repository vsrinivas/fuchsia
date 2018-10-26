// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_MAGMA_H_
#define GARNET_LIB_MACHINA_VIRTIO_MAGMA_H_

#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/macros.h>
#include <zircon/types.h>
#include <memory>
#include <string>
#include <unordered_map>
#include "garnet/lib/machina/virtio_queue_waiter.h"
#include "garnet/lib/magma/include/magma_abi/magma.h"
#include "garnet/lib/magma/include/virtio/virtio_magma.h"

namespace machina {

class VirtioQueue;
struct VirtioDescriptor;

class VirtioMagma {
 public:
  VirtioMagma(zx::vmar* vmar, async_dispatcher_t* dispatcher,
              VirtioQueue* in_queue, VirtioQueue* out_queue)
      : vmar_(vmar),
        dispatcher_(dispatcher),
        in_queue_(in_queue),
        in_queue_wait_(dispatcher, in_queue,
                       fit::bind_member(this, &VirtioMagma::OnQueueReady)),
        out_queue_(out_queue),
        out_queue_wait_(
            out_queue->event(), VirtioQueue::SIGNAL_QUEUE_AVAIL,
            fit::bind_member(this, &VirtioMagma::OnCommandAvailable)) {}
  ~VirtioMagma();
  zx_status_t Init(std::string device_path);
  void HandleCommand(uint16_t head);

 private:
  void OnCommandAvailable(async_dispatcher_t* dispatcher, async::Wait* wait,
                          zx_status_t status, const zx_packet_signal_t* signal);
  void OnQueueReady(zx_status_t status, uint16_t index);
  void Query(const virtio_magma_query_t* request,
             virtio_magma_query_resp_t* response);
  void CreateConnection(const virtio_magma_create_connection_t* request,
                        virtio_magma_create_connection_resp_t* response);
  void ReleaseConnection(const virtio_magma_release_connection_t* request,
                         virtio_magma_release_connection_resp_t* response);

  std::string device_path_;

  fxl::UniqueFD device_fd_;
  __UNUSED zx::vmar* vmar_;
  async_dispatcher_t* dispatcher_;
  __UNUSED VirtioQueue* in_queue_;
  VirtioQueueWaiter in_queue_wait_;
  VirtioQueue* out_queue_;
  async::Wait out_queue_wait_;
  std::unordered_map<uint64_t, magma_connection_t*> connections_;
  uint64_t next_connection_id_ = 1;

  FXL_DISALLOW_COPY_AND_ASSIGN(VirtioMagma);
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_MAGMA_H_
