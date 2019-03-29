// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_DEVICE_VIRTIO_MAGMA_H_
#define GARNET_BIN_GUEST_VMM_DEVICE_VIRTIO_MAGMA_H_

#include <memory>
#include <string>
#include <unordered_map>

#include <fbl/unique_fd.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <src/lib/fxl/macros.h>
#include <lib/zx/vmar.h>
#include <zircon/types.h>

#include "garnet/bin/guest/vmm/device/virtio_queue.h"
#include "garnet/lib/magma/include/magma_abi/magma.h"
#include "garnet/lib/magma/include/virtio/virtio_magma.h"

class VirtioMagma {
 public:
  VirtioMagma(zx::vmar* vmar, VirtioQueue* in_queue, VirtioQueue* out_queue)
      : vmar_(vmar), in_queue_(in_queue), out_queue_(out_queue) {}
  ~VirtioMagma();
  zx_status_t Init(std::string device_path, std::string driver_path);
  void HandleCommand(VirtioChain* chain);

  void OnCommandAvailable();
  void OnQueueReady();

 private:
  void GetDriver(const virtio_magma_get_driver_t* request,
                 virtio_magma_get_driver_resp_t* response);
  void Query(const virtio_magma_query_t* request,
             virtio_magma_query_resp_t* response);
  void CreateConnection(const virtio_magma_create_connection_t* request,
                        virtio_magma_create_connection_resp_t* response);
  void ReleaseConnection(const virtio_magma_release_connection_t* request,
                         virtio_magma_release_connection_resp_t* response);

  std::string device_path_;
  std::string driver_path_;
  fbl::unique_fd device_fd_;
  fbl::unique_fd driver_fd_;
  zx::vmo driver_vmo_;
  zx::vmar* vmar_;
  __UNUSED VirtioQueue* in_queue_;
  VirtioQueue* out_queue_;
  VirtioChain out_chain_;
  std::unordered_map<uint64_t, magma_connection_t> connections_;
  uint64_t next_connection_id_ = 1;

  FXL_DISALLOW_COPY_AND_ASSIGN(VirtioMagma);
};

#endif  // GARNET_BIN_GUEST_VMM_DEVICE_VIRTIO_MAGMA_H_
