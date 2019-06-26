// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_magma.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zx/vmar.h>
#include <src/lib/fxl/logging.h>
#include <sys/stat.h>
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <unistd.h>
#include <zircon/status.h>

#include "garnet/lib/magma/src/magma_util/macros.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

VirtioMagma::VirtioMagma(component::StartupContext* context) : DeviceBase(context) {}

void VirtioMagma::Start(fuchsia::virtualization::hardware::StartInfo start_info, zx::vmar vmar,
                        StartCallback callback) {
  auto deferred = fit::defer([&callback]() { callback(ZX_ERR_INTERNAL); });
  PrepStart(std::move(start_info));
  vmar_ = std::move(vmar);

  out_queue_.set_phys_mem(&phys_mem_);
  out_queue_.set_interrupt(
      fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioMagma::Interrupt));

  static constexpr const char* kDevicePath = "/dev/class/gpu/000";
  device_fd_ = fbl::unique_fd(open(kDevicePath, O_RDONLY));
  if (!device_fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open device at " << kDevicePath << ": " << strerror(errno);
    return;
  }
  deferred.cancel();
  callback(ZX_OK);
}

void VirtioMagma::Ready(uint32_t negotiated_features, ReadyCallback callback) {
  auto deferred = fit::defer(std::move(callback));
}

void VirtioMagma::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                                 zx_gpaddr_t used, ConfigureQueueCallback callback) {
  TRACE_DURATION("machina", "VirtioMagma::ConfigureQueue");
  auto deferred = fit::defer(std::move(callback));
  if (queue != 0) {
    FXL_LOG(ERROR) << "ConfigureQueue on non-existent queue " << queue;
    return;
  }
  out_queue_.Configure(size, desc, avail, used);
}

void VirtioMagma::NotifyQueue(uint16_t queue) {
  TRACE_DURATION("machina", "VirtioMagma::NotifyQueue");
  if (queue != 0) {
    return;
  }
  VirtioChain out_chain;
  while (out_queue_.NextChain(&out_chain)) {
    VirtioMagmaGeneric::HandleCommand(std::move(out_chain));
  }
}

zx_status_t VirtioMagma::Handle_query(const virtio_magma_query_ctrl_t* request,
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

zx_status_t VirtioMagma::Handle_map_aligned(const virtio_magma_map_aligned_ctrl_t* request,
                                            virtio_magma_map_aligned_resp_t* response) {
  FXL_LOG(ERROR) << "Specialized map calls should be converted by the driver into generic ones";
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioMagma::Handle_map_specific(const virtio_magma_map_specific_ctrl_t* request,
                                             virtio_magma_map_specific_resp_t* response) {
  FXL_LOG(ERROR) << "Specialized map calls should be converted by the driver into generic ones";
  return ZX_ERR_NOT_SUPPORTED;
}

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  VirtioMagma virtio_magma(context.get());
  return loop.Run();
}
