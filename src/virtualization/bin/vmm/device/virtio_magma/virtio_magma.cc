// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_magma/virtio_magma.h"

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
#include <zircon/status.h>

#include "src/virtualization/bin/vmm/device/virtio_magma/magma_image.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

static constexpr const char* kDeviceDir = "/dev/class/gpu";

#if VIRTMAGMA_DEBUG
#include <lib/syslog/global.h>
#define LOG_VERBOSE(msg, ...) _FX_LOGF(FX_LOG_INFO, "virtio_magma", msg, ##__VA_ARGS__)
#else
#define LOG_VERBOSE(msg, ...)
#endif

static zx_status_t InitFromImageInfo(zx::vmo vmo, ImageInfoWithToken& info,
                                     fuchsia::virtualization::hardware::VirtioImage* image) {
  if (info.token) {
    zx_status_t status = info.token.duplicate(ZX_RIGHT_SAME_RIGHTS, &image->token);
    if (status != ZX_OK)
      return status;
  }

  image->vmo = std::move(vmo);

  image->info.resize(sizeof(magma_image_info_t));
  memcpy(image->info.data(), &info.info, sizeof(magma_image_info_t));

  return ZX_OK;
}

static void InitFromVirtioImage(fuchsia::virtualization::hardware::VirtioImage& image, zx::vmo* vmo,
                                ImageInfoWithToken* info) {
  *vmo = std::move(image.vmo);
  info->token = std::move(image.token);

  assert(image.info.size() >= sizeof(magma_image_info_t));
  memcpy(&info->info, image.info.data(), sizeof(magma_image_info_t));
}

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
    if (strcmp(entry->d_name, ".") == 0) {
      continue;
    }
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

// Returns the notification data after the response struct.
zx_status_t VirtioMagma::Handle_read_notification_channel2(VirtioDescriptor* request_desc,
                                                           VirtioDescriptor* response_desc,
                                                           uint32_t* used_out) {
  auto request_copy =
      *reinterpret_cast<virtio_magma_read_notification_channel2_ctrl_t*>(request_desc->addr);

  virtio_magma_read_notification_channel2_resp_t response{};

  if (response_desc->len < sizeof(response) + request_copy.buffer_size) {
    FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_READ_NOTIFICATION_CHANNEL2: response descriptor too small";
    return ZX_ERR_INVALID_ARGS;
  }

  auto notification_buffer =
      reinterpret_cast<virtio_magma_read_notification_channel2_resp_t*>(response_desc->addr) + 1;

  request_copy.buffer = reinterpret_cast<uint64_t>(notification_buffer);

  zx_status_t status =
      VirtioMagmaGeneric::Handle_read_notification_channel2(&request_copy, &response);
  if (status != ZX_OK)
    return status;

  if (response.result_return == MAGMA_STATUS_OK) {
    // For testing
    constexpr uint32_t kMagicFlags = 0xabcd1234;
    if (request_copy.hdr.flags == kMagicFlags && request_copy.buffer_size >= sizeof(kMagicFlags) &&
        response.buffer_size_out == 0) {
      memcpy(notification_buffer, &kMagicFlags, sizeof(kMagicFlags));
      response.buffer_size_out = sizeof(kMagicFlags);
    }
  }

  memcpy(response_desc->addr, &response, sizeof(response));
  *used_out = static_cast<uint32_t>(sizeof(response) + response.buffer_size_out);

  return ZX_OK;
}

// Returns the buffer size after the response struct.
zx_status_t VirtioMagma::Handle_get_buffer_handle2(VirtioDescriptor* request_desc,
                                                   VirtioDescriptor* response_desc,
                                                   uint32_t* used_out) {
  virtio_magma_get_buffer_handle2_resp_t response = {
      .hdr = {.type = VIRTIO_MAGMA_RESP_GET_BUFFER_HANDLE2}};

  if (response_desc->len < sizeof(response) + sizeof(uint64_t)) {
    FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_GET_BUFFER_HANDLE2: response descriptor too small";
    return ZX_ERR_INVALID_ARGS;
  }

  auto request_copy =
      *reinterpret_cast<virtio_magma_get_buffer_handle2_ctrl_t*>(request_desc->addr);

  zx::vmo vmo;
  response.result_return =
      magma_get_buffer_handle2(request_copy.buffer, vmo.reset_and_get_address());
  response.handle_out = vmo.get();

  memcpy(response_desc->addr, &response, sizeof(response));
  *used_out = sizeof(response);

  if (response.result_return == MAGMA_STATUS_OK) {
    uint64_t buffer_size;
    zx_status_t status = vmo.get_size(&buffer_size);
    if (status != ZX_OK)
      return status;

    void* buffer_size_ptr =
        reinterpret_cast<virtio_magma_get_buffer_handle2_resp_t*>(response_desc->addr) + 1;
    memcpy(buffer_size_ptr, &buffer_size, sizeof(buffer_size));
    *used_out += sizeof(buffer_size);

    // Keep the handle alive while the guest is referencing it.
    stored_handles_.push_back(std::move(vmo));
  }

  return ZX_OK;
}

// Returns the buffer size after the response struct.
zx_status_t VirtioMagma::Handle_query(VirtioDescriptor* request_desc,
                                      VirtioDescriptor* response_desc, uint32_t* used_out) {
  virtio_magma_query_resp_t response = {.hdr = {.type = VIRTIO_MAGMA_RESP_QUERY}};

  auto request_copy = *reinterpret_cast<virtio_magma_query_ctrl_t*>(request_desc->addr);

  const auto device = request_copy.device;
  const auto id = request_copy.id;

  zx::vmo vmo;
  uint64_t result;

  response.result_return = magma_query(device, id, vmo.reset_and_get_address(), &result);

  if (response_desc->len < sizeof(response) + (vmo.is_valid() ? sizeof(uint64_t) : 0)) {
    FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_QUERY: response descriptor too small";
    return ZX_ERR_INVALID_ARGS;
  }

  response.result_buffer_out = vmo.get();
  response.result_out = result;

  memcpy(response_desc->addr, &response, sizeof(response));
  *used_out = sizeof(response);

  if (response.result_return == MAGMA_STATUS_OK && vmo.is_valid()) {
    uint64_t buffer_size;
    zx_status_t status = vmo.get_size(&buffer_size);
    if (status != ZX_OK)
      return status;

    void* buffer_size_ptr = reinterpret_cast<virtio_magma_query_resp_t*>(response_desc->addr) + 1;
    memcpy(buffer_size_ptr, &buffer_size, sizeof(buffer_size));
    *used_out += sizeof(buffer_size);

    // Keep the handle alive while the guest is referencing it.
    stored_handles_.push_back(std::move(vmo));
  }

  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_device_import(const virtio_magma_device_import_ctrl_t* request,
                                              virtio_magma_device_import_resp_t* response) {
  zx::channel server_handle, client_handle;
  zx_status_t status = zx::channel::create(0u, &server_handle, &client_handle);
  if (status != ZX_OK)
    return status;
  status = fdio_service_connect(device_path_.c_str(), server_handle.release());
  if (status != ZX_OK) {
    LOG_VERBOSE("fdio_service_connect failed %d", status);
    return status;
  }

  auto modified = *request;
  modified.device_channel = client_handle.release();
  return VirtioMagmaGeneric::Handle_device_import(&modified, response);
}

zx_status_t VirtioMagma::Handle_release_connection(
    const virtio_magma_release_connection_ctrl_t* request,
    virtio_magma_release_connection_resp_t* response) {
  zx_status_t status = VirtioMagmaGeneric::Handle_release_connection(request, response);
  if (status != ZX_OK)
    return ZX_OK;

  auto connection = reinterpret_cast<magma_connection_t>(request->connection);

  connection_image_map_.erase(connection);

  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_release_buffer(const virtio_magma_release_buffer_ctrl_t* request,
                                               virtio_magma_release_buffer_resp_t* response) {
  zx_status_t status = VirtioMagmaGeneric::Handle_release_buffer(request, response);
  if (status != ZX_OK)
    return ZX_OK;

  auto connection = reinterpret_cast<magma_connection_t>(request->connection);

  const uint64_t buffer = request->buffer;
  connection_image_map_[connection].erase(buffer);

  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_internal_map2(const virtio_magma_internal_map2_ctrl_t* request,
                                              virtio_magma_internal_map2_resp_t* response) {
  FX_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_INTERNAL_MAP2);

  response->address_out = 0;
  response->hdr.type = VIRTIO_MAGMA_RESP_INTERNAL_MAP2;

  zx::unowned_vmo vmo(static_cast<zx_handle_t>(request->buffer));

  // Buffer handle must have been stored.
  auto iter = std::find(stored_handles_.begin(), stored_handles_.end(), vmo->get());
  if (iter == stored_handles_.end()) {
    response->result_return = MAGMA_STATUS_INVALID_ARGS;
    return ZX_OK;
  }

  const uint64_t length = request->length;

  zx_vaddr_t zx_vaddr;
  zx_status_t zx_status = vmar_.map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0 /*vmar_offset*/, *vmo,
                                    0 /* vmo_offset */, length, &zx_vaddr);
  if (zx_status != ZX_OK) {
    FX_LOGS(ERROR) << "vmar map (length " << length << ") failed: " << zx_status;
    response->result_return = MAGMA_STATUS_INVALID_ARGS;
    return zx_status;
  }

  buffer_maps2_.emplace(zx_vaddr, std::pair<zx_handle_t, size_t>(vmo->get(), length));

  response->address_out = zx_vaddr;

  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_internal_unmap2(const virtio_magma_internal_unmap2_ctrl_t* request,
                                                virtio_magma_internal_unmap2_resp_t* response) {
  FX_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_INTERNAL_UNMAP2);

  response->hdr.type = VIRTIO_MAGMA_RESP_INTERNAL_UNMAP2;

  const uintptr_t address = request->address;
  const uint64_t buffer = request->buffer;

  auto iter = buffer_maps2_.find(address);
  if (iter == buffer_maps2_.end()) {
    response->result_return = MAGMA_STATUS_INVALID_ARGS;
    return ZX_OK;
  }

  const auto& mapping = iter->second;
  const zx_handle_t buffer_handle = mapping.first;
  const size_t length = mapping.second;

  if (buffer_handle != buffer) {
    response->result_return = MAGMA_STATUS_INVALID_ARGS;
    return ZX_OK;
  }

  buffer_maps2_.erase(iter);

  zx_status_t zx_status = vmar_.unmap(address, length);
  if (zx_status != ZX_OK)
    return zx_status;

  response->result_return = MAGMA_STATUS_OK;
  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_internal_release_handle(
    const virtio_magma_internal_release_handle_ctrl_t* request,
    virtio_magma_internal_release_handle_resp_t* response) {
  FX_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_INTERNAL_RELEASE_HANDLE);

  response->hdr.type = VIRTIO_MAGMA_RESP_INTERNAL_RELEASE_HANDLE;

  auto iter = std::find(stored_handles_.begin(), stored_handles_.end(), request->handle);

  if (iter == stored_handles_.end()) {
    response->result_return = MAGMA_STATUS_INVALID_ARGS;
  } else {
    stored_handles_.erase(iter);
    response->result_return = MAGMA_STATUS_OK;
  }

  return ZX_OK;
}

// Poll items are after the request struct, and they are updated to reflect status.
zx_status_t VirtioMagma::Handle_poll(VirtioDescriptor* request_desc,
                                     VirtioDescriptor* response_desc, uint32_t* used_out) {
  auto request_ptr = reinterpret_cast<uint8_t*>(request_desc->addr);

  auto request = *reinterpret_cast<virtio_magma_poll_ctrl_t*>(request_ptr);

  // request.count is a byte count
  if (request_desc->len < sizeof(virtio_magma_poll_ctrl_t) + request.count) {
    FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_POLL: request descriptor too small";
    return ZX_ERR_INVALID_ARGS;
  }

  if (request.count % sizeof(magma_poll_item_t) != 0) {
    FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_POLL: count is not a multiple of sizeof(magma_poll_item_t)";
    return ZX_ERR_INVALID_ARGS;
  }

  // Transform byte count back to item count
  request.count /= sizeof(magma_poll_item_t);

  // The actual items immediately follow the request struct.
  std::vector<magma_poll_item_t> items(request.count);
  memcpy(items.data(), request_ptr + sizeof(request), items.size() * sizeof(magma_poll_item_t));

  request.items = reinterpret_cast<uint64_t>(items.data());

  virtio_magma_poll_resp_t response{};

  zx_status_t status = VirtioMagmaGeneric::Handle_poll(&request, &response);

  if (status == ZX_OK) {
    // Copy the items back into the request descriptor.
    memcpy(request_ptr + sizeof(request), items.data(), items.size() * sizeof(magma_poll_item_t));
  }

  memcpy(response_desc->addr, &response, sizeof(response));
  *used_out = sizeof(response);

  return status;
}

zx_status_t VirtioMagma::Handle_export(const virtio_magma_export_ctrl_t* request,
                                       virtio_magma_export_resp_t* response) {
  if (!wayland_importer_) {
    LOG_VERBOSE("driver attempted to export a buffer without wayland present");
    response->hdr.type = VIRTIO_MAGMA_RESP_EXPORT;
    response->buffer_handle_out = 0;
    response->result_return = MAGMA_STATUS_UNIMPLEMENTED;
    return ZX_OK;
  }

  // We only export images
  fuchsia::virtualization::hardware::VirtioImage image;

  {
    auto& image_map =
        connection_image_map_[reinterpret_cast<magma_connection_t>(request->connection)];

    const uint64_t buffer = request->buffer;
    auto iter = image_map.find(buffer);
    if (iter == image_map.end()) {
      response->hdr.type = VIRTIO_MAGMA_RESP_EXPORT;
      response->buffer_handle_out = 0;
      response->result_return = MAGMA_STATUS_INVALID_ARGS;
      return ZX_OK;
    }

    ImageInfoWithToken& info = iter->second;

    // Get the VMO handle for this buffer.
    zx_status_t status = VirtioMagmaGeneric::Handle_export(request, response);
    if (status != ZX_OK) {
      LOG_VERBOSE("VirtioMagmaGeneric::Handle_export failed: %d", status);
      return status;
    }

    // Take ownership of the VMO handle.
    zx::vmo vmo(static_cast<zx_handle_t>(response->buffer_handle_out));
    response->buffer_handle_out = 0;

    status = InitFromImageInfo(std::move(vmo), info, &image);
    if (status != ZX_OK) {
      LOG_VERBOSE("InitFromImageInfo failed: %d", status);
      return status;
    }
  }

  // TODO(fxbug.dev/13261): improvement backlog
  // Perform a blocking import of the image, then return the VFD ID in the response.
  // Note that since the virtio-magma device is fully synchronous anyway, this does
  // not impact performance. Ideally, the device would stash the response chain and
  // return it only when the Import call returns, processing messages from other
  // instances, or even other connections, in the meantime.
  uint32_t vfd_id;
  zx_status_t status = wayland_importer_->ImportImage(std::move(image), &vfd_id);
  if (status != ZX_OK) {
    LOG_VERBOSE("ImportImage failed: %d", status);
    return status;
  }

  response->buffer_handle_out = vfd_id;

  return ZX_OK;
}

zx_status_t VirtioMagma::Handle_import(const virtio_magma_import_ctrl_t* request,
                                       virtio_magma_import_resp_t* response) {
  if (!wayland_importer_) {
    LOG_VERBOSE("driver attempted to import a buffer without wayland present");
    response->hdr.type = VIRTIO_MAGMA_RESP_IMPORT;
    response->result_return = MAGMA_STATUS_UNIMPLEMENTED;
    return ZX_OK;
  }

  uint32_t vfd_id = request->buffer_handle;

  std::unique_ptr<fuchsia::virtualization::hardware::VirtioImage> image;
  zx_status_t result;

  zx_status_t status = wayland_importer_->ExportImage(vfd_id, &result, &image);
  if (status != ZX_OK) {
    LOG_VERBOSE("VirtioWl ExportImage failed: %d", status);
    return status;
  }
  assert(image);

  if (result != ZX_OK) {
    LOG_VERBOSE("VirtioWl ExportImage returned result: %d", status);
    return result;
  }

  ImageInfoWithToken info;
  zx::vmo vmo;

  InitFromVirtioImage(*image.get(), &vmo, &info);

  {
    virtio_magma_import_ctrl_t request_copy = *request;
    request_copy.buffer_handle = vmo.release();

    status = VirtioMagmaGeneric::Handle_import(&request_copy, response);
    if (status != ZX_OK)
      return status;
  }

  {
    auto& image_map =
        connection_image_map_[reinterpret_cast<magma_connection_t>(request->connection)];

    image_map[response->buffer_out] = std::move(info);
  }

  return ZX_OK;
}

// Command structures come after the request struct.
zx_status_t VirtioMagma::Handle_execute_command(VirtioDescriptor* request_desc,
                                                VirtioDescriptor* response_desc,
                                                uint32_t* used_out) {
  auto request_ptr = reinterpret_cast<uint8_t*>(request_desc->addr);

  auto request = *reinterpret_cast<virtio_magma_execute_command_ctrl_t*>(request_ptr);

  struct WireDescriptor {
    uint32_t resource_count;
    uint32_t command_buffer_count;
    uint32_t wait_semaphore_count;
    uint32_t signal_semaphore_count;
    uint64_t flags;
  };

  auto descriptor = *reinterpret_cast<WireDescriptor*>(request_ptr + sizeof(request));

  size_t resources_size = sizeof(magma_exec_resource) * descriptor.resource_count;
  size_t command_buffers_size = sizeof(magma_exec_command_buffer) * descriptor.command_buffer_count;
  size_t semaphore_ids_size =
      sizeof(uint64_t) * (descriptor.wait_semaphore_count + descriptor.signal_semaphore_count);

  size_t required_bytes = sizeof(request) + sizeof(descriptor) + command_buffers_size +
                          resources_size + semaphore_ids_size;
  if (request_desc->len < required_bytes) {
    FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_EXECUTE_COMMAND: request descriptor too small";
    return ZX_ERR_INVALID_ARGS;
  }

  std::vector<magma_exec_command_buffer> command_buffers(descriptor.command_buffer_count);
  memcpy(command_buffers.data(), request_ptr + sizeof(request) + sizeof(descriptor),
         command_buffers_size);

  std::vector<magma_exec_resource> resources(descriptor.resource_count);
  memcpy(resources.data(),
         request_ptr + sizeof(request) + sizeof(descriptor) + command_buffers_size, resources_size);

  std::vector<uint64_t> semaphore_ids(descriptor.wait_semaphore_count +
                                      descriptor.signal_semaphore_count);
  memcpy(semaphore_ids.data(),
         request_ptr + sizeof(request) + sizeof(descriptor) + resources_size + command_buffers_size,
         semaphore_ids_size);

  magma_command_descriptor magma_descriptor = {
      .resource_count = descriptor.resource_count,
      .command_buffer_count = descriptor.command_buffer_count,
      .wait_semaphore_count = descriptor.wait_semaphore_count,
      .signal_semaphore_count = descriptor.signal_semaphore_count,
      .resources = resources.data(),
      .command_buffers = command_buffers.data(),
      .semaphore_ids = semaphore_ids.data(),
      .flags = descriptor.flags};

  request.descriptor = reinterpret_cast<uintptr_t>(&magma_descriptor);

  virtio_magma_execute_command_resp_t response{};

  zx_status_t status = VirtioMagmaGeneric::Handle_execute_command(&request, &response);

  if (status == ZX_OK) {
    memcpy(response_desc->addr, &response, sizeof(response));
    *used_out = sizeof(response);
  }

  return status;
}

// Image create info comes after the request struct.
zx_status_t VirtioMagma::Handle_virt_create_image(VirtioDescriptor* request_desc,
                                                  VirtioDescriptor* response_desc,
                                                  uint32_t* used_out) {
  magma_connection_t connection{};
  magma_image_create_info_t image_create_info{};
  {
    auto request = reinterpret_cast<virtio_magma_virt_create_image_ctrl_t*>(request_desc->addr);

    connection = reinterpret_cast<magma_connection_t>(request->connection);
    image_create_info = *reinterpret_cast<magma_image_create_info_t*>(request + 1);

    if (request_desc->len < sizeof(*request) + sizeof(magma_image_create_info_t)) {
      FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_VIRT_CREATE_IMAGE: request descriptor too small";
      return ZX_ERR_INVALID_ARGS;
    }
  }

  ImageInfoWithToken image_info;
  zx::vmo vmo;

  virtio_magma_virt_create_image_resp_t response = {
      .hdr = {.type = VIRTIO_MAGMA_RESP_VIRT_CREATE_IMAGE}};

  // Assuming the current connection is on the one and only physical device.
  uint32_t physical_device_index = 0;
  response.result_return = magma_image::CreateDrmImage(physical_device_index, &image_create_info,
                                                       &image_info.info, &vmo, &image_info.token);

  if (response.result_return == MAGMA_STATUS_OK) {
    magma_buffer_t image;
    response.result_return = magma_import(connection, vmo.release(), &image);

    if (response.result_return == MAGMA_STATUS_OK) {
      response.image_out = image;

      auto& map = connection_image_map_[connection];

      map[response.image_out] = std::move(image_info);
    }
  }

  memcpy(response_desc->addr, &response, sizeof(response));
  *used_out = sizeof(response);

  return ZX_OK;
}

// Image info comes after the request struct.
zx_status_t VirtioMagma::Handle_virt_get_image_info(VirtioDescriptor* request_desc,
                                                    VirtioDescriptor* response_desc,
                                                    uint32_t* used_out) {
  auto request_copy =
      *reinterpret_cast<virtio_magma_virt_get_image_info_ctrl_t*>(request_desc->addr);

  if (request_desc->len < sizeof(request_copy) + sizeof(magma_image_info_t)) {
    FX_LOGS(ERROR) << "VIRTIO_MAGMA_CMD_VIRT_GET_IMAGE_INFO: request descriptor too small";
    return ZX_ERR_INVALID_ARGS;
  }

  auto connection = reinterpret_cast<magma_connection_t>(request_copy.connection);

  auto& map = connection_image_map_[connection];

  virtio_magma_virt_get_image_info_resp_t response = {
      .hdr = {.type = VIRTIO_MAGMA_RESP_VIRT_GET_IMAGE_INFO}};

  response.result_return = MAGMA_STATUS_INVALID_ARGS;

  auto iter = map.find(request_copy.image);
  if (iter != map.end()) {
    ImageInfoWithToken& image_info = iter->second;

    auto image_info_out = reinterpret_cast<magma_image_info_t*>(
        reinterpret_cast<virtio_magma_virt_get_image_info_ctrl_t*>(request_desc->addr) + 1);
    *image_info_out = image_info.info;

    response.result_return = MAGMA_STATUS_OK;
  }

  memcpy(response_desc->addr, &response, sizeof(response));
  *used_out = sizeof(response);

  return ZX_OK;
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
