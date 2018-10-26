// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_magma.h"
#include <fcntl.h>
#include <lib/fxl/logging.h>
#include <unistd.h>
#include "garnet/lib/machina/device/virtio_queue.h"

namespace machina {

zx_status_t VirtioMagma::Init(std::string device_path) {
  device_path_ = device_path;
  device_fd_ = fxl::UniqueFD(open(device_path_.c_str(), O_RDONLY));
  if (!device_fd_.is_valid()) {
    FXL_LOG(ERROR) << "Failed do open device at " << device_path_ << ": "
                   << strerror(errno);
    return ZX_ERR_INTERNAL;
  }

  return out_queue_wait_.Begin(dispatcher_);
}

VirtioMagma::~VirtioMagma() {
  // TODO: flush and close all host magma connections
}

#define COMMAND_CASE(cmd_suffix, struct_prefix, method_name)                  \
  case VIRTIO_MAGMA_CMD_##cmd_suffix: {                                       \
    auto request = reinterpret_cast<const virtio_magma_##struct_prefix##_t*>( \
        request_desc.addr);                                                   \
    auto response = reinterpret_cast<virtio_magma_##struct_prefix##_resp_t*>( \
        response_desc.addr);                                                  \
    if (response_desc.len >= sizeof(*response)) {                             \
      method_name(request, response);                                         \
      used = sizeof(*response);                                               \
    } else {                                                                  \
      FXL_LOG(ERROR) << "MAGMA command "                                      \
                     << "(" << command_type << ") "                           \
                     << "response descriptor too small";                      \
    }                                                                         \
  } break

void VirtioMagma::HandleCommand(uint16_t head) {
  VirtioDescriptor request_desc;
  zx_status_t status = out_queue_->ReadDesc(head, &request_desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read descriptor";
    return;
  }
  const auto request_header =
      reinterpret_cast<virtio_magma_ctrl_hdr_t*>(request_desc.addr);
  const uint32_t command_type = request_header->type;

  uint32_t used = 0;
  if (request_desc.has_next) {
    VirtioDescriptor response_desc{};
    if (request_desc.has_next) {
      status = out_queue_->ReadDesc(request_desc.next, &response_desc);
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to read descriptor";
        return;
      }
    }
    if (response_desc.writable) {
      switch (command_type) {
        COMMAND_CASE(QUERY, query, Query);
        COMMAND_CASE(CREATE_CONNECTION, create_connection, CreateConnection);
        COMMAND_CASE(RELEASE_CONNECTION, release_connection, ReleaseConnection);
        default: {
          FXL_LOG(ERROR) << "Unsupported MAGMA command "
                         << "(" << command_type << ")";
          auto response =
              reinterpret_cast<virtio_magma_ctrl_hdr_t*>(response_desc.addr);
          response->type = VIRTIO_MAGMA_RESP_ERR_INVALID_COMMAND;
          used = sizeof(*response);
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

  status = out_queue_->Return(head, used);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to return descriptor to queue " << status;
    return;
  }

  // Begin waiting on next command.
  status = out_queue_wait_.Begin(dispatcher_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to begin waiting for commands";
  }
}

void VirtioMagma::OnCommandAvailable(async_dispatcher_t* dispatcher,
                                     async::Wait* wait, zx_status_t status,
                                     const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed while waiting on commands: " << status;
    return;
  }

  uint16_t out_queue_index = 0;
  status = out_queue_->NextAvail(&out_queue_index);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to get next available queue index";
    return;
  }

  HandleCommand(out_queue_index);
}

void VirtioMagma::OnQueueReady(zx_status_t status, uint16_t index) {}

void VirtioMagma::Query(const virtio_magma_query_t* request,
                        virtio_magma_query_resp_t* response) {
  FXL_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_QUERY);
  if (request->field_id == MAGMA_QUERY_DEVICE_ID ||
      request->field_id >= MAGMA_QUERY_VENDOR_PARAM_0) {
    uint64_t field_value_out = 0;
    magma_status_t status =
        magma_query(device_fd_.get(), request->field_id, &field_value_out);
    response->hdr.type = VIRTIO_MAGMA_RESP_QUERY;
    response->field_value_out = field_value_out;
    response->status_return = status;
  } else {
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_INVALID_ARGUMENT;
  }
}

void VirtioMagma::CreateConnection(
    const virtio_magma_create_connection_t* request,
    virtio_magma_create_connection_resp_t* response) {
  FXL_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_CREATE_CONNECTION);
  auto connection = magma_create_connection(device_fd_.get(), 0);
  if (!connection) {
    response->connection_return = -1;
    response->hdr.type = VIRTIO_MAGMA_RESP_ERR_HOST_DISCONNECTED;
    return;
  }
  FXL_LOG(INFO) << "magma connection created (" << next_connection_id_ << ")";
  connections_.insert({next_connection_id_, connection});
  response->connection_return = next_connection_id_++;
  response->hdr.type = VIRTIO_MAGMA_RESP_CREATE_CONNECTION;
}

void VirtioMagma::ReleaseConnection(
    const virtio_magma_release_connection_t* request,
    virtio_magma_release_connection_resp_t* response) {
  FXL_DCHECK(request->hdr.type == VIRTIO_MAGMA_CMD_RELEASE_CONNECTION);
  auto connection = connections_.find(request->connection);
  if (connection == connections_.end()) {
    FXL_LOG(ERROR) << "invalid connection (" << request->connection << ")";
    return;
  }
  FXL_LOG(INFO) << "magma connection released (" << request->connection << ")";
  connections_.erase(connection);
}

}  // namespace machina
