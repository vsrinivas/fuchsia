// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_magma.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/status.h>

#include "src/virtualization/bin/vmm/device/virtio_queue.h"

static constexpr const char* kDeviceDir = "/dev/class/gpu";

VirtioMagma::VirtioMagma(sys::ComponentContext* context) : DeviceBase(context) {}

void VirtioMagma::Start(
    fuchsia::virtualization::hardware::StartInfo start_info, zx::vmar vmar,
    fidl::InterfaceHandle<fuchsia::virtualization::hardware::VirtioWaylandImporter>
        wayland_importer,
    StartCallback callback) {
  auto deferred = fit::defer([&callback]() { callback(ZX_ERR_INTERNAL); });
  if (wayland_importer) {
    wayland_importer_ = wayland_importer.BindSync();
  }
  PrepStart(std::move(start_info));
  vmar_ = std::move(vmar);

  out_queue_.set_phys_mem(&phys_mem_);
  out_queue_.set_interrupt(
      fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioMagma::Interrupt));

  auto dir = opendir(kDeviceDir);
  if (!dir) {
    FX_LOGS(ERROR) << "Failed to open device directory at " << kDeviceDir << ": "
                   << strerror(errno);
    deferred.cancel();
    callback(ZX_ERR_NOT_FOUND);
    return;
  }

  for (auto entry = readdir(dir); entry != nullptr && !device_fd_.is_valid();
       entry = readdir(dir)) {
    device_path_ = std::string(kDeviceDir) + "/" + entry->d_name;
    device_fd_ = fbl::unique_fd(open(device_path_.c_str(), O_RDONLY));
    if (!device_fd_.is_valid()) {
      FX_LOGS(WARNING) << "Failed to open device at " << device_path_ << ": " << strerror(errno);
    }
  }

  closedir(dir);

  if (!device_fd_.is_valid()) {
    FX_LOGS(ERROR) << "Failed to open any devices in " << kDeviceDir << ".";
    deferred.cancel();
    callback(ZX_ERR_NOT_FOUND);
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
    FX_LOGS(ERROR) << "ConfigureQueue on non-existent queue " << queue;
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

zx_status_t VirtioMagma::Handle_device_import(const virtio_magma_device_import_ctrl_t* request,
                                              virtio_magma_device_import_resp_t* response) {
  zx::channel server_handle, client_handle;
  zx_status_t status = zx::channel::create(0u, &server_handle, &client_handle);
  if (status != ZX_OK)
    return status;
  status = fdio_service_connect(device_path_.c_str(), server_handle.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_service_connect failed - " << status;
    return status;
  }

  auto modified = *request;
  modified.device_channel = client_handle.release();
  return VirtioMagmaGeneric::Handle_device_import(&modified, response);
}

zx_status_t VirtioMagma::Handle_create_buffer(const virtio_magma_create_buffer_ctrl_t* request,
                                              virtio_magma_create_buffer_resp_t* response) {
  zx_status_t status = VirtioMagmaGeneric::Handle_create_buffer(request, response);
  if (status != ZX_OK) {
    return status;
  }
  zx::vmar vmar;
  status = vmar_.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmar);
  if (status != ZX_OK) {
    return status;
  }
  magma_status_t magma_status =
      magma_set_buffer_mapping_address_range(response->buffer_out, vmar.release());
  if (magma_status != MAGMA_STATUS_OK) {
    FX_LOGS(ERROR) << "magma_set_buffer_mapping_address_range failed - " << magma_status;
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_map_aligned(const virtio_magma_map_aligned_ctrl_t* request,
                                            virtio_magma_map_aligned_resp_t* response) {
  FX_LOGS(ERROR) << "Specialized map calls should be converted by the driver into generic ones";
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioMagma::Handle_map_specific(const virtio_magma_map_specific_ctrl_t* request,
                                             virtio_magma_map_specific_resp_t* response) {
  FX_LOGS(ERROR) << "Specialized map calls should be converted by the driver into generic ones";
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t VirtioMagma::Handle_wait_semaphores(const virtio_magma_wait_semaphores_ctrl_t* request,
                                                virtio_magma_wait_semaphores_resp_t* response) {
  auto request_mod = *request;
  // The actual semaphores immediately follow the request struct.
  request_mod.semaphores = reinterpret_cast<uint64_t>(&request[1]);
  return VirtioMagmaGeneric::Handle_wait_semaphores(&request_mod, response);
}

zx_status_t VirtioMagma::Handle_poll(const virtio_magma_poll_ctrl_t* request,
                                     virtio_magma_poll_resp_t* response) {
  auto request_mod = *request;
  // The actual items immediately follow the request struct.
  request_mod.items = reinterpret_cast<uint64_t>(&request[1]);
  // Transform byte count back to item count
  request_mod.count /= sizeof(magma_poll_item_t);
  return VirtioMagmaGeneric::Handle_poll(&request_mod, response);
}

zx_status_t VirtioMagma::Handle_read_notification_channel(
    const virtio_magma_read_notification_channel_ctrl_t* request,
    virtio_magma_read_notification_channel_resp_t* response) {
  auto request_mod = *request;
  // The notification data immediately follows the response struct.
  request_mod.buffer = reinterpret_cast<uint64_t>(&response[1]);
  return VirtioMagmaGeneric::Handle_read_notification_channel(&request_mod, response);
}

zx_status_t VirtioMagma::Handle_export(const virtio_magma_export_ctrl_t* request,
                                       virtio_magma_export_resp_t* response) {
  if (!wayland_importer_) {
    FX_LOGS(INFO) << "driver attempted to export a buffer without wayland present";
    response->hdr.type = VIRTIO_MAGMA_RESP_EXPORT;
    response->buffer_handle_out = 0;
    response->result_return = MAGMA_STATUS_UNIMPLEMENTED;
    return ZX_OK;
  }
  zx_status_t status = VirtioMagmaGeneric::Handle_export(request, response);
  if (status != ZX_OK) {
    return status;
  }
  // Handle_export calls magma_export, which in turn returns a native handle type
  // of the caller's platform.
  zx::vmo exported_vmo(static_cast<zx_handle_t>(response->buffer_handle_out));
  response->buffer_handle_out = 0;
  uint32_t vfd_id = 0;
  // TODO(fxbug.dev/13261): improvement backlog
  // Perform a blocking import of the VMO, then return the VFD ID in the response.
  // Note that since the virtio-magma device is fully synchronous anyway, this does
  // not impact performance. Ideally, the device would stash the response chain and
  // return it only when the Import call returns, processing messages from other
  // instances, or even other connections, in the meantime.
  status = wayland_importer_->Import(std::move(exported_vmo), &vfd_id);
  if (status != ZX_OK) {
    return status;
  }
  response->buffer_handle_out = vfd_id;
  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_execute_command_buffer_with_resources(
    const virtio_magma_execute_command_buffer_with_resources_ctrl_t* request,
    virtio_magma_execute_command_buffer_with_resources_resp_t* response) {
  // Command buffer payload comes immediately after the request
  auto command_buffer = reinterpret_cast<magma_system_command_buffer*>(
      const_cast<virtio_magma_execute_command_buffer_with_resources_ctrl_t*>(request) + 1);
  auto exec_resources = reinterpret_cast<magma_system_exec_resource*>(command_buffer + 1);
  auto semaphore_ids = reinterpret_cast<uint64_t*>(exec_resources + command_buffer->resource_count);

  virtio_magma_execute_command_buffer_with_resources_ctrl_t request_dupe;
  memcpy(&request_dupe, request, sizeof(request_dupe));

  request_dupe.command_buffer = reinterpret_cast<uintptr_t>(command_buffer);
  request_dupe.resources = reinterpret_cast<uintptr_t>(exec_resources);
  request_dupe.semaphore_ids = reinterpret_cast<uintptr_t>(semaphore_ids);

  return VirtioMagmaGeneric::Handle_execute_command_buffer_with_resources(&request_dupe, response);
}

int main(int argc, char** argv) {
  syslog::SetTags({"virtio_magma"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  VirtioMagma virtio_magma(context.get());
  return loop.Run();
}
