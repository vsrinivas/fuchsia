// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio_wl.h"

#include <lib/zx/socket.h>

#include "garnet/lib/machina/dev_mem.h"
#include "lib/fxl/logging.h"

static constexpr uint32_t kMapFlags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;

namespace machina {
namespace {

// Vfd type that holds a region of memory that is mapped into the guest's
// physical address space. The memory region is unmapped when instance is
// destroyed.
class Memory : public VirtioWl::Vfd {
 public:
  Memory(zx::vmo vmo, uintptr_t addr, uint64_t size, zx::vmar* vmar)
      : VirtioWl::Vfd(vmo.release()), addr_(addr), size_(size), vmar_(vmar) {}
  ~Memory() override { vmar_->unmap(addr_, size_); }

  // Create a memory instance by mapping |vmo| into |vmar|. Returns a valid
  // instance on success.
  static std::unique_ptr<Memory> Create(zx::vmo vmo, zx::vmar* vmar) {
    // Get the VMO size that has been rounded up to the next page size boundary.
    uint64_t size;
    zx_status_t status = vmo.get_size(&size);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed get VMO size: " << status;
      return nullptr;
    }

    // Map memory into VMAR. |addr| is guaranteed to be page-aligned and
    // non-zero on success.
    zx_gpaddr_t addr;
    status = vmar->map(0, vmo, 0, size, kMapFlags, &addr);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to map VMO into guest VMAR: " << status;
      return nullptr;
    }

    return std::make_unique<Memory>(std::move(vmo), addr, size, vmar);
  }

  uintptr_t addr() const { return addr_; }
  uint64_t size() const { return size_; }

 private:
  const uintptr_t addr_;
  const uint64_t size_;
  zx::vmar* const vmar_;
};

// Vfd type that holds a wayland dispatcher connection.
class Connection : public VirtioWl::Vfd {
 public:
  explicit Connection(zx::channel channel) : VirtioWl::Vfd(channel.release()) {}
};

// Vfd type that holds a socket for data transfers.
class Pipe : public VirtioWl::Vfd {
 public:
  Pipe(zx::socket socket, zx::socket remote_socket)
      : VirtioWl::Vfd(remote_socket.release()), socket_(std::move(socket)) {}

 private:
  zx::socket socket_;
};

}  // namespace

VirtioWl::VirtioWl(const PhysMem& phys_mem, zx::vmar vmar,
                   async_dispatcher_t* dispatcher,
                   OnNewConnectionCallback on_new_connection_callback)
    : VirtioInprocessDevice(phys_mem, VIRTIO_WL_F_TRANS_FLAGS),
      vmar_(std::move(vmar)),
      dispatcher_(dispatcher),
      on_new_connection_callback_(std::move(on_new_connection_callback)) {}

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

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(request->size, 0, &vmo);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to allocate VMO (size=" << request->size
                   << "): " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  std::unique_ptr<Memory> vfd = Memory::Create(std::move(vmo), &vmar_);
  if (!vfd) {
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  zx_gpaddr_t addr = vfd->addr();
  uint64_t size = vfd->size();

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
  response->pfn = addr / PAGE_SIZE;
  response->size = size;
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
  num_bytes -= request->vfd_count * sizeof(*vfds);
  if (num_bytes > ZX_CHANNEL_MAX_MSG_BYTES) {
    FXL_LOG(ERROR) << "Message too large for channel (size=" << num_bytes
                   << ")";
    response->type = VIRTIO_WL_RESP_ERR;
    return;
  }

  if (request->vfd_count > ZX_CHANNEL_MAX_MSG_HANDLES) {
    FXL_LOG(ERROR) << "Too many VFDs for message (vfds=" << request->vfd_count
                   << ")";
    response->type = VIRTIO_WL_RESP_ERR;
    return;
  }

  zx::handle handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (uint32_t i = 0; i < request->vfd_count; ++i) {
    auto it = vfds_.find(vfds[i]);
    if (it == vfds_.end()) {
      response->type = VIRTIO_WL_RESP_INVALID_ID;
      return;
    }

    zx_status_t status =
        it->second->handle().duplicate(ZX_RIGHT_SAME_RIGHTS, &handles[i]);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to duplicate handle: " << status;
      response->type = VIRTIO_WL_RESP_INVALID_ID;
      return;
    }
  }

  // The handles are consumed by zx_channel_write call below.
  zx_handle_t raw_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
  for (uint32_t i = 0; i < request->vfd_count; ++i) {
    raw_handles[i] = handles[i].release();
  }
  zx_status_t status =
      zx_channel_write(it->second->handle().get(), 0, vfds + request->vfd_count,
                       num_bytes, raw_handles, request->vfd_count);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to write message to channel: " << status;
    response->type = VIRTIO_WL_RESP_ERR;
    return;
  }

  response->type = VIRTIO_WL_RESP_OK;
}

void VirtioWl::HandleNewCtx(const virtio_wl_ctrl_vfd_new_t* request,
                            virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "virtio_wl_new_ctx");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  zx::channel channel, remote_channel;
  zx_status_t status =
      zx::channel::create(ZX_SOCKET_STREAM, &channel, &remote_channel);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel: " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  auto vfd = std::make_unique<Connection>(std::move(channel));
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

  on_new_connection_callback_(std::move(remote_channel));

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

  zx::socket socket, remote_socket;
  zx_status_t status =
      zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create socket: " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  auto vfd =
      std::make_unique<Pipe>(std::move(socket), std::move(remote_socket));
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

}  // namespace machina
