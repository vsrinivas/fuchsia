// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_magma.h"

#include <fcntl.h>
#include <lib/zx/vmar.h>
#include <src/lib/fxl/logging.h>
#include <sys/stat.h>
#include <trace/event.h>
#include <unistd.h>
#include <zircon/status.h>

#include "garnet/lib/magma/src/magma_util/macros.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

zx_status_t VirtioMagma::Init(const zx::vmar& vmar) {
  static constexpr const char* kDevicePath = "/dev/class/gpu/000";
  device_fd_ = fbl::unique_fd(open(kDevicePath, O_RDONLY));
  if (!device_fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open device at " << kDevicePath << ": "
                   << strerror(errno);
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = vmar.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmar_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to duplicate device vmar for magma - " << zx_status_get_string(status);
    return status;
  }

  return ZX_OK;
}

VirtioMagma::~VirtioMagma() {
  // TODO: flush and close all host magma connections
}

void VirtioMagma::OnCommandAvailable() {
  TRACE_DURATION("machina", "VirtioMagma::OnCommandAvailable");
  while (out_queue_->NextChain(&out_chain_)) {
    HandleCommand(&out_chain_);
  }
}

void VirtioMagma::OnQueueReady() {}

zx_status_t VirtioMagma::Handle_query(
  const virtio_magma_query_ctrl_t* request,
  virtio_magma_query_resp_t* response) {
  auto modified = *request;
  modified.file_descriptor = device_fd_.get();
  return VirtioMagmaGeneric::Handle_query(&modified, response);
}

zx_status_t VirtioMagma::Handle_create_connection(
  const virtio_magma_create_connection_ctrl_t* request,
  virtio_magma_create_connection_resp_t* response) {
  auto modified = *request;
  modified.file_descriptor = device_fd_.get();
  return VirtioMagmaGeneric::Handle_create_connection(&modified, response);
}

zx_status_t VirtioMagma::Handle_map_aligned(
  const virtio_magma_map_aligned_ctrl_t* request,
  virtio_magma_map_aligned_resp_t* response) {
  FXL_LOG(ERROR) << "Specialized map calls should be converted by the driver into generic ones";
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioMagma::Handle_map_specific(
  const virtio_magma_map_specific_ctrl_t* request,
  virtio_magma_map_specific_resp_t* response) {
  FXL_LOG(ERROR) << "Specialized map calls should be converted by the driver into generic ones";
  return ZX_ERR_NOT_SUPPORTED;
}
