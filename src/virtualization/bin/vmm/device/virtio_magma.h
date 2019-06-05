// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_

#include <fbl/unique_fd.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/vmar.h>
#include <src/lib/fxl/macros.h>
#include <zircon/types.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "garnet/lib/magma/include/magma_abi/magma.h"
#include "garnet/lib/magma/include/virtio/virtio_magma.h"
#include "src/virtualization/bin/vmm/device/virtio_magma_generic.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

class VirtioMagma : public VirtioMagmaGeneric {
 public:
  VirtioMagma(VirtioQueue* out_queue) : out_queue_(out_queue) {}
  virtual ~VirtioMagma();
  zx_status_t Init(const zx::vmar& vmar);

  void OnCommandAvailable();
  void OnQueueReady();

 private:
  virtual zx_status_t Handle_query(
    const virtio_magma_query_ctrl_t* request,
    virtio_magma_query_resp_t* response) override;
  virtual zx_status_t Handle_create_connection(
    const virtio_magma_create_connection_ctrl_t* request,
    virtio_magma_create_connection_resp_t* response) override;
  virtual zx_status_t Handle_map_aligned(
    const virtio_magma_map_aligned_ctrl_t* request,
    virtio_magma_map_aligned_resp_t* response) override;
  virtual zx_status_t Handle_map_specific(
    const virtio_magma_map_specific_ctrl_t* request,
    virtio_magma_map_specific_resp_t* response) override;

  fbl::unique_fd device_fd_;
  zx::vmar vmar_;
  VirtioQueue* out_queue_;
  VirtioChain out_chain_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VirtioMagma);
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_MAGMA_H_
