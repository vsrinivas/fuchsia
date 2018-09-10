// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_wl.h"

#include "garnet/lib/machina/dev_mem.h"
#include "lib/fxl/logging.h"

namespace machina {

class VirtioWl::Vfd {
 public:
  Vfd(zx::vmar* vmar, uintptr_t addr, uint64_t size, zx::vmo vmo)
      : vmar_(vmar), addr_(addr), size_(size), vmo_(std::move(vmo)) {}
  ~Vfd() { vmar_->unmap(addr_, size_); }

 private:
  zx::vmar* const vmar_;
  const uintptr_t addr_;
  const uint64_t size_;
  zx::vmo vmo_;
};

VirtioWl::VirtioWl(const PhysMem& phys_mem, zx::vmar vmar,
                   async_dispatcher_t* dispatcher)
    : VirtioInprocessDevice(phys_mem, VIRTIO_WL_F_TRANS_FLAGS),
      vmar_(std::move(vmar)),
      dispatcher_(dispatcher) {}

VirtioWl::~VirtioWl() = default;

zx_status_t VirtioWl::Init() {
  return out_queue()->PollAsync(
      dispatcher_, &out_queue_wait_,
      fit::bind_member(this, &VirtioWl::HandleCommand));
}

zx_status_t VirtioWl::HandleCommand(VirtioQueue* queue, uint16_t head,
                                    uint32_t* used) {
  VirtioDescriptor request_desc;
  zx_status_t status = queue->ReadDesc(head, &request_desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read descriptor";
    return status;
  }
  const auto request_header =
      reinterpret_cast<virtio_wl_ctrl_hdr_t*>(request_desc.addr);
  const uint32_t command_type = request_header->type;

  TRACE_DURATION("machina", "virtio_wl_command", "type", command_type);
  if (!request_desc.has_next) {
    FXL_LOG(ERROR) << "WL command "
                   << "(" << command_type << ") "
                   << "does not contain a response descriptor";
    return ZX_OK;
  }

  VirtioDescriptor response_desc;
  status = queue->ReadDesc(request_desc.next, &response_desc);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to read descriptor";
    return status;
  }

  switch (command_type) {
    case VIRTIO_WL_CMD_VFD_NEW: {
      auto request =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNew(request, response);
      *used += sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_CLOSE: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      HandleClose(request, response);
      *used += sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_SEND: {
      auto request =
          reinterpret_cast<virtio_wl_ctrl_vfd_send_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      HandleSend(request, request_desc.len, response);
      *used += sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_NEW_CTX: {
      auto request =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNewCtx(request, response);
      *used += sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_NEW_PIPE: {
      auto request =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNewPipe(request, response);
      *used += sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_NEW_DMABUF: {
      auto request =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response =
          reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNewDmabuf(request, response);
      *used += sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_DMABUF_SYNC: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_dmabuf_sync_t*>(
          request_desc.addr);
      auto response =
          reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      HandleDmabufSync(request, response);
      *used += sizeof(*response);
    } break;
    default: {
      FXL_LOG(ERROR) << "Unsupported WL command "
                     << "(" << command_type << ")";
      auto response =
          reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      response->type = VIRTIO_WL_RESP_INVALID_CMD;
      *used += sizeof(*response);
      break;
    }
  }

  return ZX_OK;
}

void VirtioWl::HandleNew(const virtio_wl_ctrl_vfd_new_t* request,
                         virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "virtio_wl_new");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  zx_gpaddr_t guest_addr;
  uint64_t actual_size;
  std::unique_ptr<Vfd> vfd =
      AllocateMemory(request->size, &guest_addr, &actual_size);
  if (!vfd) {
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  bool inserted;
  std::tie(std::ignore, inserted) =
      vfds_.insert({request->vfd_id, std::move(vfd)});
  if (!inserted) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  response->hdr.type = VIRTIO_WL_RESP_VFD_NEW;
  response->hdr.flags = 0;
  response->vfd_id = request->vfd_id;
  response->flags = VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE;
  response->pfn = guest_addr / PAGE_SIZE;
  response->size = actual_size;
}

void VirtioWl::HandleClose(const virtio_wl_ctrl_vfd_t* request,
                           virtio_wl_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_wl_close");

  if (vfds_.erase(request->vfd_id)) {
    response->type = VIRTIO_WL_RESP_OK;
  } else {
    response->type = VIRTIO_WL_RESP_INVALID_ID;
  }
}

void VirtioWl::HandleSend(const virtio_wl_ctrl_vfd_send_t* request,
                          uint32_t request_len,
                          virtio_wl_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_wl_send");

  auto it = vfds_.find(request->vfd_id);
  if (it == vfds_.end()) {
    response->type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  auto vfds = reinterpret_cast<const uint32_t*>(request + 1);
  uint32_t num_bytes = request_len - sizeof(*request);

  if (num_bytes < request->vfd_count * sizeof(*vfds)) {
    response->type = VIRTIO_WL_RESP_ERR;
    return;
  }

  for (uint32_t i = 0; i < request->vfd_count; ++i) {
    auto it = vfds_.find(vfds[i]);
    if (it == vfds_.end()) {
      response->type = VIRTIO_WL_RESP_INVALID_ID;
      return;
    }

    // TODO(reveman): Duplicate handle for message.
  }

  // TODO(reveman): Write message to server channel.
  //
  // zx_channel_write(server_handle,
  //                  options,
  //                  reinterpret_cast<void*>(vfds + request->vfd_count),
  //                  num_bytes - request->vfd_count * sizeof(*vfds),
  //                  handles,
  //                  num_handles);
  //
  response->type = VIRTIO_WL_RESP_OK;
}

void VirtioWl::HandleNewCtx(const virtio_wl_ctrl_vfd_new_t* request,
                            virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "virtio_wl_new_ctx");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  bool inserted;
  std::tie(std::ignore, inserted) = vfds_.insert({request->vfd_id, nullptr});
  if (!inserted) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  response->hdr.type = VIRTIO_WL_RESP_VFD_NEW;
  response->hdr.flags = 0;
  response->vfd_id = request->vfd_id;
  response->flags = VIRTIO_WL_VFD_WRITE | VIRTIO_WL_VFD_READ;
  response->pfn = 0;
  response->size = 0;
}

void VirtioWl::HandleNewPipe(const virtio_wl_ctrl_vfd_new_t* request,
                             virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "virtio_wl_new_pipe");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  bool inserted;
  std::tie(std::ignore, inserted) = vfds_.insert({request->vfd_id, nullptr});
  if (!inserted) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  response->hdr.type = VIRTIO_WL_RESP_VFD_NEW;
  response->hdr.flags = 0;
  response->vfd_id = request->vfd_id;
  response->flags = VIRTIO_WL_VFD_READ;
  response->pfn = 0;
  response->size = 0;
}

void VirtioWl::HandleNewDmabuf(const virtio_wl_ctrl_vfd_new_t* request,
                               virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "virtio_wl_new_dmabuf");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  FXL_LOG(ERROR) << __FUNCTION__ << ": Not implemented";
  response->hdr.type = VIRTIO_WL_RESP_INVALID_CMD;
}

void VirtioWl::HandleDmabufSync(const virtio_wl_ctrl_vfd_dmabuf_sync_t* request,
                                virtio_wl_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "virtio_wl_dmabuf_sync");

  auto it = vfds_.find(request->vfd_id);
  if (it == vfds_.end()) {
    response->type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  // TODO(reveman): Add synchronization code when using GPU buffers.
  response->type = VIRTIO_WL_RESP_OK;
}

std::unique_ptr<VirtioWl::Vfd> VirtioWl::AllocateMemory(uint32_t size,
                                                        uintptr_t* guest_addr,
                                                        uint64_t* actual_size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to allocate VMO (size=" << size
                   << "): " << status;
    return nullptr;
  }

  status = vmo.get_size(actual_size);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed get VMO size: " << status;
    return nullptr;
  }

  // TODO(reveman): Remove ZX_VM_PERM_EXECUTE when MAC-166 has been resolved.
  status = vmar_.map(0, vmo, 0, *actual_size,
                     ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE,
                     guest_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to map VMO into guest vmar: " << status;
    return nullptr;
  }

  return std::make_unique<Vfd>(&vmar_, *guest_addr, *actual_size,
                               std::move(vmo));
}

}  // namespace machina
