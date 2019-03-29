// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/device/virtio_magma.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <src/lib/fxl/logging.h>
#include <lib/zx/vmar.h>
#include <trace/event.h>

#include "garnet/bin/guest/vmm/device/virtio_queue.h"
#include "garnet/lib/magma/src/magma_util/macros.h"

zx_status_t VirtioMagma::Init(std::string device_path,
                              std::string driver_path) {
  device_path_ = std::move(device_path);
  driver_path_ = std::move(driver_path);
  device_fd_ = fbl::unique_fd(open(device_path_.c_str(), O_RDONLY));
  if (!device_fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to open device at " << device_path_ << ": "
                   << strerror(errno);
    return ZX_ERR_INTERNAL;
  }

  driver_fd_ = fbl::unique_fd(open(driver_path_.c_str(), O_RDONLY));
  if (!driver_fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed to load driver from " << driver_path_ << ": "
                   << strerror(errno);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

VirtioMagma::~VirtioMagma() {
  // TODO: flush and close all host magma connections
}

#define COMMAND_CASE(cmd_suffix, struct_prefix, method_name, used)            \
  case VIRTIO_MAGMA_CMD_##cmd_suffix: {                                       \
    auto request = reinterpret_cast<const virtio_magma_##struct_prefix##_t*>( \
        request_desc.addr);                                                   \
    auto response = reinterpret_cast<virtio_magma_##struct_prefix##_resp_t*>( \
        response_desc.addr);                                                  \
    if (response_desc.len >= sizeof(*response)) {                             \
      method_name(request, response);                                         \
      (used) = sizeof(*response);                                             \
    } else {                                                                  \
      FXL_LOG(ERROR) << "MAGMA command "                                      \
                     << "(" << command_type << ") "                           \
                     << "response descriptor too small";                      \
    }                                                                         \
  } break

void VirtioMagma::HandleCommand(VirtioChain* chain) {
  TRACE_DURATION("machina", "VirtioMagma::HandleCommand");
  VirtioDescriptor request_desc;
  if (!chain->NextDescriptor(&request_desc)) {
    FXL_LOG(ERROR) << "Failed to read request descriptor";
    return;
  }
  const auto request_header =
      reinterpret_cast<virtio_magma_ctrl_hdr_t*>(request_desc.addr);
  const uint32_t command_type = request_header->type;

  if (chain->HasDescriptor()) {
    VirtioDescriptor response_desc{};
    if (!chain->NextDescriptor(&response_desc)) {
      FXL_LOG(ERROR) << "Failed to read descriptor";
      return;
    }
    if (response_desc.writable) {
      switch (command_type) {
        COMMAND_CASE(GET_DRIVER, get_driver, GetDriver, *chain->Used());
        COMMAND_CASE(QUERY, query, Query, *chain->Used());
        COMMAND_CASE(CREATE_CONNECTION, create_connection, CreateConnection,
                     *chain->Used());
        COMMAND_CASE(RELEASE_CONNECTION, release_connection, ReleaseConnection,
                     *chain->Used());
        default: {
          FXL_LOG(ERROR) << "Unsupported MAGMA command "
                         << "(" << command_type << ")";
          auto response =
              reinterpret_cast<virtio_magma_ctrl_hdr_t*>(response_desc.addr);
          response->type = VIRTIO_MAGMA_RESP_ERR_INVALID_COMMAND;
          *chain->Used() = sizeof(*response);
        } break;
      }
    } else {
      FXL_LOG(ERROR) << "MAGMA command "
                     << "(" << command_type << ") "
                     << "response descriptor is not writable";
    }
  } else {
    FXL_LOG(ERROR) << "MAGMA command "
                   << "(" << command_type << ") "
                   << "does not contain a response descriptor";
  }
  chain->Return();
}

void VirtioMagma::OnCommandAvailable() {
  TRACE_DURATION("machina", "VirtioMagma::OnCommandAvailable");
  while (out_queue_->NextChain(&out_chain_)) {
    HandleCommand(&out_chain_);
  }
}

void VirtioMagma::OnQueueReady() {}

void VirtioMagma::GetDriver(const virtio_magma_get_driver_t* request,
                            virtio_magma_get_driver_resp_t* response) {
  FXL_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_GET_DRIVER);
  if (driver_vmo_.is_valid()) {
    FXL_LOG(ERROR) << "Driver already provided to guest";
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_INVALID_COMMAND;
    return;
  }

  struct stat driver_fd_stats {};
  if (fstat(driver_fd_.get(), &driver_fd_stats) != 0) {
    FXL_LOG(ERROR) << "Failed to get stats for driver fd: " << strerror(errno);
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_INTERNAL;
    return;
  }

  size_t driver_vmo_size =
      magma::round_up(driver_fd_stats.st_size, request->page_size);
  zx::vmo driver_vmo;
  zx_status_t status = zx::vmo::create(driver_vmo_size, 0, &driver_vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to allocate VMO (size=" << driver_vmo_size
                   << "): " << status;
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_OUT_OF_MEMORY;
    return;
  }

  zx_gpaddr_t driver_vmo_addr_guest{};
  // TODO(MAC-520): unmap the driver from guest physmem after it has been copied
  // to the filesystem
  status = vmar_->map(0, driver_vmo, 0, driver_vmo_size, ZX_VM_PERM_READ,
                      &driver_vmo_addr_guest);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map VMO into guest VMAR: " << status;
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_INTERNAL;
    return;
  }
  FXL_DCHECK(driver_vmo_addr_guest % request->page_size == 0);

  zx_gpaddr_t driver_vmo_addr_host{};
  status = zx::vmar::root_self()->map(
      0, driver_vmo, 0, driver_vmo_size,
      ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE,
      &driver_vmo_addr_host);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map VMO: " << status;
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_INTERNAL;
    return;
  }

  // TODO(MA-533): use file io helper to read file to VMO
  ssize_t bytes_read =
      read(driver_fd_.get(), reinterpret_cast<void*>(driver_vmo_addr_host),
           driver_fd_stats.st_size);
  if (bytes_read != driver_fd_stats.st_size) {
    FXL_LOG(ERROR) << "Failed to read from driver fd: " << strerror(errno);
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_INTERNAL;
    status = vmar_->unmap(driver_vmo_addr_host, driver_vmo_size);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to unmap VMO: " << status;
    }
    return;
  }

  driver_vmo_ = std::move(driver_vmo);
  response->hdr.type = VIRTIO_MAGMA_RESP_GET_DRIVER;
  response->pfn = driver_vmo_addr_guest / request->page_size;
  response->size = driver_fd_stats.st_size;
}

void VirtioMagma::Query(const virtio_magma_query_t* request,
                        virtio_magma_query_resp_t* response) {
  TRACE_DURATION("machina", "VirtioMagma::Query");
  FXL_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_QUERY);
  uint64_t field_value_out = 0;
  magma_status_t status =
      magma_query(device_fd_.get(), request->field_id, &field_value_out);
  response->hdr.type = VIRTIO_MAGMA_RESP_QUERY;
  response->field_value_out = field_value_out;
  response->status_return = status;
}

void VirtioMagma::CreateConnection(
    const virtio_magma_create_connection_t* request,
    virtio_magma_create_connection_resp_t* response) {
  TRACE_DURATION("machina", "VirtioMagma::CreateConnection");
  FXL_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_CREATE_CONNECTION);
  magma_connection_t connection;
  magma_status_t status =
      magma_create_connection(device_fd_.get(), &connection);
  if (status != MAGMA_STATUS_OK) {
    response->connection_return = -1;
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_HOST_DISCONNECTED;
    return;
  }
  connections_.insert({next_connection_id_, connection});
  response->connection_return = next_connection_id_++;
  response->hdr.type = VIRTIO_MAGMA_RESP_CREATE_CONNECTION;
}

void VirtioMagma::ReleaseConnection(
    const virtio_magma_release_connection_t* request,
    virtio_magma_release_connection_resp_t* response) {
  TRACE_DURATION("machina", "VirtioMagma::ReleaseConnection");
  FXL_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_RELEASE_CONNECTION);
  auto connection = connections_.find(request->connection);
  if (connection == connections_.end()) {
    FXL_LOG(ERROR) << "invalid connection (" << request->connection << ")";
    return;
  }
  connections_.erase(connection);
}
