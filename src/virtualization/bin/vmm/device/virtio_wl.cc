// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/virtio_wl.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>
#include <lib/trace/event.h>
#include <lib/zx/socket.h>

#include <vector>

#include "src/virtualization/bin/vmm/bits.h"

static constexpr uint32_t DRM_FORMAT_ARGB8888 = 0x34325241;
static constexpr uint32_t DRM_FORMAT_ABGR8888 = 0x34324241;
static constexpr uint32_t DRM_FORMAT_XRGB8888 = 0x34325258;
static constexpr uint32_t DRM_FORMAT_XBGR8888 = 0x34324258;

// Vfd type that holds a region of memory that is mapped into the guest's
// physical address space. The memory region is unmapped when instance is
// destroyed.
class Memory : public VirtioWl::Vfd {
 public:
  Memory(zx::vmo vmo, uintptr_t addr, uint64_t size, zx::vmar* vmar)
      : handle_(vmo.release()), addr_(addr), size_(size), vmar_(vmar) {}
  ~Memory() override { vmar_->unmap(addr_, size_); }

  // Create a memory instance by mapping |vmo| into |vmar|. Returns a valid
  // instance on success.
  static std::unique_ptr<Memory> Create(zx::vmo vmo, zx::vmar* vmar, uint32_t map_flags) {
    TRACE_DURATION("machina", "Memory::Create");
    // Get the VMO size that has been rounded up to the next page size boundary.
    uint64_t size;
    zx_status_t status = vmo.get_size(&size);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed get VMO size: " << status;
      return nullptr;
    }

    // Map memory into VMAR. |addr| is guaranteed to be page-aligned and
    // non-zero on success.
    zx_gpaddr_t addr;
    status = vmar->map(0, vmo, 0, size, map_flags, &addr);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to map VMO into guest VMAR: " << status;
      return nullptr;
    }

    return std::make_unique<Memory>(std::move(vmo), addr, size, vmar);
  }

  // |VirtioWl::Vfd|
  zx_status_t Duplicate(zx::handle* handle) override {
    return handle_.duplicate(ZX_RIGHT_SAME_RIGHTS, handle);
  }

  uintptr_t addr() const { return addr_; }
  uint64_t size() const { return size_; }

 private:
  zx::handle handle_;
  const uintptr_t addr_;
  const uint64_t size_;
  zx::vmar* const vmar_;
};

// Vfd type that holds a wayland dispatcher connection.
class Connection : public VirtioWl::Vfd {
 public:
  Connection(zx::channel channel, async::Wait::Handler handler)
      : channel_(std::move(channel)),
        wait_(channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, 0, std::move(handler)) {
  }
  ~Connection() override { wait_.Cancel(); }

  // |VirtioWl::Vfd|
  zx_status_t BeginWaitOnData() override { return wait_.Begin(async_get_default_dispatcher()); }
  zx_status_t AvailableForRead(uint32_t* bytes, uint32_t* handles) override {
    TRACE_DURATION("machina", "Connection::AvailableForRead");
    zx_status_t status = channel_.read(0, nullptr, nullptr, 0u, 0u, bytes, handles);
    return status == ZX_ERR_BUFFER_TOO_SMALL ? ZX_OK : status;
  }
  zx_status_t Read(void* bytes, zx_handle_info_t* handles, uint32_t num_bytes, uint32_t num_handles,
                   uint32_t* actual_bytes, uint32_t* actual_handles) override {
    TRACE_DURATION("machina", "Connection::Read");
    if (bytes == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    return channel_.read_etc(0, bytes, handles, num_bytes, num_handles, actual_bytes,
                             actual_handles);
  }
  zx_status_t Write(const void* bytes, uint32_t num_bytes, const zx_handle_t* handles,
                    uint32_t num_handles, size_t* actual_bytes) override {
    TRACE_DURATION("machina", "Connection::Write");
    // All bytes are always writting to the channel.
    *actual_bytes = num_bytes;
    return channel_.write(0, bytes, num_bytes, handles, num_handles);
  }

 private:
  zx::channel channel_;
  async::Wait wait_;
};

// Vfd type that holds a socket for data transfers.
class Pipe : public VirtioWl::Vfd {
 public:
  Pipe(zx::socket socket, zx::socket remote_socket, async::Wait::Handler rx_handler,
       async::Wait::Handler tx_handler)
      : socket_(std::move(socket)),
        remote_socket_(std::move(remote_socket)),
        rx_wait_(socket_.get(), ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED, 0,
                 std::move(rx_handler)),
        tx_wait_(socket_.get(), ZX_SOCKET_WRITABLE, 0, std::move(tx_handler)) {}
  ~Pipe() override {
    rx_wait_.Cancel();
    tx_wait_.Cancel();
  }

  // |VirtioWl::Vfd|
  zx_status_t BeginWaitOnData() override { return rx_wait_.Begin(async_get_default_dispatcher()); }

  zx_status_t AvailableForRead(uint32_t* bytes, uint32_t* handles) override {
    TRACE_DURATION("machina", "Pipe::AvailableForRead");
    zx_info_socket_t info = {};
    zx_status_t status = socket_.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
    if (status != ZX_OK) {
      return status;
    }
    if (bytes) {
      *bytes = info.rx_buf_available;
    }
    if (handles) {
      *handles = 0;
    }
    return ZX_OK;
  }

  zx_status_t Read(void* bytes, zx_handle_info_t* handles, uint32_t num_bytes, uint32_t num_handles,
                   uint32_t* actual_bytes, uint32_t* actual_handles) override {
    TRACE_DURATION("machina", "Pipe::Read");
    size_t actual;
    zx_status_t status = socket_.read(0, bytes, num_bytes, &actual);
    if (status != ZX_OK) {
      return status;
    }
    if (actual_bytes) {
      *actual_bytes = actual;
    }
    if (actual_handles) {
      *actual_handles = 0;
    }
    return ZX_OK;
  }
  zx_status_t BeginWaitOnWritable() override {
    return tx_wait_.Begin(async_get_default_dispatcher());
  }
  zx_status_t Write(const void* bytes, uint32_t num_bytes, const zx_handle_t* handles,
                    uint32_t num_handles, size_t* actual_bytes) override {
    TRACE_DURATION("machina", "Pipe::Write");
    // Handles can't be sent over sockets.
    if (num_handles) {
      while (num_handles--) {
        zx_handle_close(handles[num_handles]);
      }
      return ZX_ERR_NOT_SUPPORTED;
    }
    return socket_.write(0, bytes, num_bytes, actual_bytes);
  }
  zx_status_t Duplicate(zx::handle* handle) override {
    zx_handle_t h = ZX_HANDLE_INVALID;
    zx_status_t status = zx_handle_duplicate(remote_socket_.get(), ZX_RIGHT_SAME_RIGHTS, &h);
    handle->reset(h);
    return status;
  }

 private:
  zx::socket socket_;
  zx::socket remote_socket_;
  async::Wait rx_wait_;
  async::Wait tx_wait_;
};

VirtioWl::VirtioWl(sys::ComponentContext* context) : DeviceBase(context) {}

void VirtioWl::Start(fuchsia::virtualization::hardware::StartInfo start_info, zx::vmar vmar,
                     fidl::InterfaceHandle<fuchsia::virtualization::WaylandDispatcher> dispatcher,
                     StartCallback callback) {
  auto deferred = fit::defer(std::move(callback));
  PrepStart(std::move(start_info));
  vmar_ = std::move(vmar);
  dispatcher_ = dispatcher.Bind();

  // Configure device queues.
  for (auto& queue : queues_) {
    queue.set_phys_mem(&phys_mem_);
    queue.set_interrupt(fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioWl::Interrupt));
  }
}

void VirtioWl::GetImporter(
    fidl::InterfaceRequest<fuchsia::virtualization::hardware::VirtioWaylandImporter> request) {
  importer_bindings_.AddBinding(this, std::move(request));
}

void VirtioWl::Import(zx::vmo vmo, ImportCallback callback) {
  TRACE_DURATION("machina", "VirtioWl::Import");
  auto deferred = fit::defer(
      [&]() { callback(fuchsia::virtualization::hardware::kVirtioWaylandInvalidVfdId); });
  zx_info_handle_basic_t handle_basic_info{};
  zx_status_t status = zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_BASIC, &handle_basic_info,
                                          sizeof(handle_basic_info), nullptr, nullptr);
  if (status != ZX_OK || handle_basic_info.type != ZX_OBJ_TYPE_VMO) {
    FX_LOGS(ERROR) << "failed to import VMO";
    return;
  }
  uint32_t vfd_id = next_vfd_id_++;
  PendingVfd pending_vfd{};
  pending_vfd.handle_info.handle = vmo.release();
  pending_vfd.handle_info.type = handle_basic_info.type;
  pending_vfd.handle_info.rights = handle_basic_info.rights;
  pending_vfd.vfd_id = vfd_id;
  pending_vfds_.push_back(std::move(pending_vfd));
  DispatchPendingEvents();
  deferred.cancel();
  callback(vfd_id);
}

void VirtioWl::Ready(uint32_t negotiated_features, ReadyCallback callback) {
  auto deferred = fit::defer(std::move(callback));
}

void VirtioWl::ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                              zx_gpaddr_t used, ConfigureQueueCallback callback) {
  TRACE_DURATION("machina", "VirtioWl::ConfigureQueue");
  auto deferred = fit::defer(std::move(callback));
  switch (queue) {
    case VIRTWL_VQ_IN:
    case VIRTWL_VQ_OUT:
      queues_[queue].Configure(size, desc, avail, used);
      break;
    default:
      FX_LOGS(ERROR) << "ConfigureQueue on non-existent queue " << queue;
      break;
  }
}

void VirtioWl::NotifyQueue(uint16_t queue) {
  TRACE_DURATION("machina", "VirtioWl::NotifyQueue");
  switch (queue) {
    case VIRTWL_VQ_IN:
      DispatchPendingEvents();
      break;
    case VIRTWL_VQ_OUT:
      OnCommandAvailable();
      break;
    default:
      break;
  }
}

void VirtioWl::HandleCommand(VirtioChain* chain) {
  VirtioDescriptor request_desc;
  if (!chain->NextDescriptor(&request_desc)) {
    FX_LOGS(ERROR) << "Failed to read descriptor";
    return;
  }
  const auto request_header = reinterpret_cast<virtio_wl_ctrl_hdr_t*>(request_desc.addr);
  const uint32_t command_type = request_header->type;

  TRACE_DURATION("machina", "VirtioWl::HandleCommand", "type", command_type);
  if (!chain->HasDescriptor()) {
    FX_LOGS(ERROR) << "WL command "
                   << "(" << command_type << ") "
                   << "does not contain a response descriptor";
    return;
  }

  VirtioDescriptor response_desc;
  if (!chain->NextDescriptor(&response_desc)) {
    FX_LOGS(ERROR) << "Failed to read response descriptor";
    return;
  }

  switch (command_type) {
    case VIRTIO_WL_CMD_VFD_NEW: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNew(request, response);
      *chain->Used() = sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_CLOSE: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_t*>(request_desc.addr);
      auto response = reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      HandleClose(request, response);
      *chain->Used() = sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_SEND: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_send_t*>(request_desc.addr);
      auto response = reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      zx_status_t status = HandleSend(request, request_desc.len, response);
      // HandleSend returns ZX_ERR_SHOULD_WAIT if asynchronous wait is needed
      // to complete. Return early here instead of writing response to guest.
      // HandleCommand will be called again by OnCanWrite() when send command
      // can continue.
      if (status == ZX_ERR_SHOULD_WAIT) {
        return;
      }
      // Reset |bytes_written_for_send_request_| after send command completes.
      bytes_written_for_send_request_ = 0;
      *chain->Used() = sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_NEW_CTX: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNewCtx(request, response);
      *chain->Used() = sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_NEW_PIPE: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNewPipe(request, response);
      *chain->Used() = sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_NEW_DMABUF: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(request_desc.addr);
      auto response = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(response_desc.addr);
      HandleNewDmabuf(request, response);
      *chain->Used() = sizeof(*response);
    } break;
    case VIRTIO_WL_CMD_VFD_DMABUF_SYNC: {
      auto request = reinterpret_cast<virtio_wl_ctrl_vfd_dmabuf_sync_t*>(request_desc.addr);
      auto response = reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      HandleDmabufSync(request, response);
      *chain->Used() = sizeof(*response);
    } break;
    default: {
      FX_LOGS(ERROR) << "Unsupported WL command "
                     << "(" << command_type << ")";
      auto response = reinterpret_cast<virtio_wl_ctrl_hdr_t*>(response_desc.addr);
      response->type = VIRTIO_WL_RESP_INVALID_CMD;
      *chain->Used() = sizeof(*response);
      break;
    }
  }

  chain->Return();
}

void VirtioWl::HandleNew(const virtio_wl_ctrl_vfd_new_t* request,
                         virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "VirtioWl::HandleNew");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(request->size, 0, &vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to allocate VMO (size=" << request->size << "): " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  std::unique_ptr<Memory> vfd =
      Memory::Create(std::move(vmo), &vmar_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (!vfd) {
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  zx_gpaddr_t addr = vfd->addr();
  uint64_t size = vfd->size();

  bool inserted;
  std::tie(std::ignore, inserted) = vfds_.insert({request->vfd_id, std::move(vfd)});
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

void VirtioWl::HandleClose(const virtio_wl_ctrl_vfd_t* request, virtio_wl_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "VirtioWl::HandleClose");

  if (vfds_.erase(request->vfd_id)) {
    response->type = VIRTIO_WL_RESP_OK;
  } else {
    response->type = VIRTIO_WL_RESP_INVALID_ID;
  }
}

zx_status_t VirtioWl::HandleSend(const virtio_wl_ctrl_vfd_send_t* request, uint32_t request_len,
                                 virtio_wl_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "VirtioWl::HandleSend");

  auto it = vfds_.find(request->vfd_id);
  if (it == vfds_.end()) {
    response->type = VIRTIO_WL_RESP_INVALID_ID;
    return ZX_OK;
  }

  auto vfds = reinterpret_cast<const uint32_t*>(request + 1);
  uint32_t num_bytes = request_len - sizeof(*request);

  if (num_bytes < request->vfd_count * sizeof(*vfds)) {
    response->type = VIRTIO_WL_RESP_ERR;
    return ZX_OK;
  }
  num_bytes -= request->vfd_count * sizeof(*vfds);
  if (num_bytes > ZX_CHANNEL_MAX_MSG_BYTES) {
    FX_LOGS(ERROR) << "Message too large for channel (size=" << num_bytes << ")";
    response->type = VIRTIO_WL_RESP_ERR;
    return ZX_OK;
  }
  auto bytes = reinterpret_cast<const uint8_t*>(vfds + request->vfd_count);

  if (request->vfd_count > ZX_CHANNEL_MAX_MSG_HANDLES) {
    FX_LOGS(ERROR) << "Too many VFDs for message (vfds=" << request->vfd_count << ")";
    response->type = VIRTIO_WL_RESP_ERR;
    return ZX_OK;
  }

  while (bytes_written_for_send_request_ < num_bytes) {
    zx::handle handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    for (uint32_t i = 0; i < request->vfd_count; ++i) {
      auto it = vfds_.find(vfds[i]);
      if (it == vfds_.end()) {
        response->type = VIRTIO_WL_RESP_INVALID_ID;
        return ZX_OK;
      }

      zx_status_t status = it->second->Duplicate(&handles[i]);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to duplicate handle: " << status;
        response->type = VIRTIO_WL_RESP_INVALID_ID;
        return ZX_OK;
      }
    }

    // The handles are consumed by Write() call below.
    zx_handle_t raw_handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    for (uint32_t i = 0; i < request->vfd_count; ++i) {
      raw_handles[i] = handles[i].release();
    }
    size_t actual_bytes = 0;
    zx_status_t status = it->second->Write(bytes + bytes_written_for_send_request_,
                                           num_bytes - bytes_written_for_send_request_, raw_handles,
                                           request->vfd_count, &actual_bytes);
    if (status == ZX_OK) {
      // Increment |bytes_written_for_send_request_|. Note: It is safe to use
      // this device global variable for this as we never process more than
      // one SEND request at a time.
      bytes_written_for_send_request_ += actual_bytes;
    } else if (status == ZX_ERR_SHOULD_WAIT) {
      it->second->BeginWaitOnWritable();
      return ZX_ERR_SHOULD_WAIT;
    } else {
      if (status != ZX_ERR_PEER_CLOSED) {
        FX_LOGS(ERROR) << "Failed to write message to VFD: " << status;
        response->type = VIRTIO_WL_RESP_ERR;
        return ZX_OK;
      }
      // Silently ignore error and skip write.
      break;
    }
  }

  response->type = VIRTIO_WL_RESP_OK;
  return ZX_OK;
}

void VirtioWl::HandleNewCtx(const virtio_wl_ctrl_vfd_new_t* request,
                            virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "VirtioWl::HandleNewCtx");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  zx::channel channel, remote_channel;
  zx_status_t status = zx::channel::create(ZX_SOCKET_STREAM, &channel, &remote_channel);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create channel: " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  uint32_t vfd_id = request->vfd_id;
  auto vfd = std::make_unique<Connection>(
      std::move(channel), [this, vfd_id](async_dispatcher_t* dispatcher, async::Wait* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {
        OnDataAvailable(vfd_id, wait, status, signal);
      });
  if (!vfd) {
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  status = vfd->BeginWaitOnData();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to begin waiting on connection: " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  bool inserted;
  std::tie(std::ignore, inserted) = vfds_.insert({vfd_id, std::move(vfd)});
  if (!inserted) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  dispatcher_->OnNewConnection(std::move(remote_channel));

  response->hdr.type = VIRTIO_WL_RESP_VFD_NEW;
  response->hdr.flags = 0;
  response->vfd_id = vfd_id;
  response->flags = VIRTIO_WL_VFD_WRITE | VIRTIO_WL_VFD_READ;
  response->pfn = 0;
  response->size = 0;
}

void VirtioWl::HandleNewPipe(const virtio_wl_ctrl_vfd_new_t* request,
                             virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "VirtioWl::HandleNewPipe");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  zx::socket socket, remote_socket;
  zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket, &remote_socket);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create socket: " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  uint32_t vfd_id = request->vfd_id;
  auto vfd = std::make_unique<Pipe>(
      std::move(socket), std::move(remote_socket),
      [this, vfd_id](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                     const zx_packet_signal_t* signal) {
        OnDataAvailable(vfd_id, wait, status, signal);
      },
      fit::bind_member(this, &VirtioWl::OnCanWrite));
  if (!vfd) {
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  status = vfd->BeginWaitOnData();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to begin waiting on pipe: " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  bool inserted;
  std::tie(std::ignore, inserted) = vfds_.insert({vfd_id, std::move(vfd)});
  if (!inserted) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  response->hdr.type = VIRTIO_WL_RESP_VFD_NEW;
  response->hdr.flags = 0;
  response->vfd_id = vfd_id;
  response->flags = request->flags & (VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE);
  response->pfn = 0;
  response->size = 0;
}

// This implements dmabuf allocations that allow direct access by GPU.
void VirtioWl::HandleNewDmabuf(const virtio_wl_ctrl_vfd_new_t* request,
                               virtio_wl_ctrl_vfd_new_t* response) {
  TRACE_DURATION("machina", "VirtioWl::HandleNewDmabuf");

  if (request->vfd_id & VIRTWL_VFD_ID_HOST_MASK) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  size_t stride;
  size_t total_size;
  switch (request->dmabuf.format) {
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888: {
      // Alignment that is sufficient for all known devices.
      // TODO(fxbug.dev/12587): Use sysmem for allocation instead of making
      // alignment assumptions here.
      stride = align(request->dmabuf.width * 4, 64);
      total_size = stride * align(request->dmabuf.height, 4);
    } break;
    default:
      FX_LOGS(ERROR) << __FUNCTION__ << ": Invalid format";
      response->hdr.type = VIRTIO_WL_RESP_ERR;
      return;
  }

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(total_size, 0, &vmo);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to allocate VMO (size=" << total_size << "): " << status;
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  std::unique_ptr<Memory> vfd =
      Memory::Create(std::move(vmo), &vmar_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  if (!vfd) {
    FX_LOGS(ERROR) << "Failed to create memory instance";
    response->hdr.type = VIRTIO_WL_RESP_OUT_OF_MEMORY;
    return;
  }

  zx_gpaddr_t addr = vfd->addr();
  uint64_t size = vfd->size();

  bool inserted;
  std::tie(std::ignore, inserted) = vfds_.insert({request->vfd_id, std::move(vfd)});
  if (!inserted) {
    response->hdr.type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  response->hdr.type = VIRTIO_WL_RESP_VFD_NEW_DMABUF;
  response->hdr.flags = 0;
  response->vfd_id = request->vfd_id;
  response->flags = VIRTIO_WL_VFD_READ | VIRTIO_WL_VFD_WRITE;
  response->pfn = addr / PAGE_SIZE;
  response->size = size;
  response->dmabuf.stride0 = stride;
  response->dmabuf.stride1 = 0;
  response->dmabuf.stride2 = 0;
  response->dmabuf.offset0 = 0;
  response->dmabuf.offset1 = 0;
  response->dmabuf.offset2 = 0;
}

void VirtioWl::HandleDmabufSync(const virtio_wl_ctrl_vfd_dmabuf_sync_t* request,
                                virtio_wl_ctrl_hdr_t* response) {
  TRACE_DURATION("machina", "VirtioWl::HandleDmabufSync");

  auto it = vfds_.find(request->vfd_id);
  if (it == vfds_.end()) {
    response->type = VIRTIO_WL_RESP_INVALID_ID;
    return;
  }

  // TODO(reveman): Add synchronization code when using GPU buffers.
  response->type = VIRTIO_WL_RESP_OK;
}

void VirtioWl::OnCommandAvailable() {
  TRACE_DURATION("machina", "VirtioWl::OnCommandAvailable");
  while (out_queue()->NextChain(&out_chain_)) {
    HandleCommand(&out_chain_);
  }
}

void VirtioWl::OnDataAvailable(uint32_t vfd_id, async::Wait* wait, zx_status_t status,
                               const zx_packet_signal_t* signal) {
  TRACE_DURATION("machina", "VirtioWl::OnDataAvailable");

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed while waiting on VFD: " << status;
    return;
  }

  ready_vfds_[vfd_id] |= signal->observed & wait->trigger();
  if (signal->observed & __ZX_OBJECT_PEER_CLOSED) {
    wait->set_trigger(wait->trigger() & ~__ZX_OBJECT_PEER_CLOSED);
  }

  DispatchPendingEvents();
}

void VirtioWl::OnCanWrite(async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                          const zx_packet_signal_t* signal) {
  TRACE_DURATION("machina", "VirtioWl::OnCanWrite");

  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed while waiting on VFD: " << status;
    return;
  }

  HandleCommand(&out_chain_);
}

void VirtioWl::DispatchPendingEvents() {
  TRACE_DURATION("machina", "VirtioWl::DispatchPendingEvents");

  // If we still need to send some NEW_VFD commands into the guest do that now.
  // This will happen if the available ring is empty when trying to send a
  // previous RECV command into the guest.
  //
  // Since these are associated with a previous event, we don't want to process
  // more events until these have been completed.
  if (!pending_vfds_.empty()) {
    if (!CreatePendingVfds()) {
      // There are still VFDs waiting on descriptors, continue waiting for more
      // descriptors to complete these.
      return;
    }
  }

  zx_status_t status;
  while (!ready_vfds_.empty() && in_queue()->HasAvail()) {
    auto it = ready_vfds_.begin();
    auto vfd_it = vfds_.find(it->first);
    if (vfd_it == vfds_.end()) {
      // Ignore entry if ID is no longer valid.
      it = ready_vfds_.erase(it);
      continue;
    }

    // Handle the case where the only signal left is PEER_CLOSED.
    if (it->second == __ZX_OBJECT_PEER_CLOSED) {
      VirtioChain chain;
      VirtioDescriptor desc;
      if (!AcquireWritableDescriptor(in_queue(), &chain, &desc)) {
        break;
      }
      if (desc.len < sizeof(virtio_wl_ctrl_vfd_t)) {
        FX_LOGS(ERROR) << "Descriptor is too small for HUP message";
        return;
      }
      auto header = static_cast<virtio_wl_ctrl_vfd_t*>(desc.addr);
      header->hdr.type = VIRTIO_WL_CMD_VFD_HUP;
      header->hdr.flags = 0;
      header->vfd_id = it->first;
      *chain.Used() = sizeof(*header);
      chain.Return();
      ready_vfds_.erase(it);
      continue;
    }

    // VFD must be in READABLE state if not in PEER_CLOSED.
    FX_CHECK(it->second & __ZX_OBJECT_READABLE) << "VFD must be readable";

    // Determine the number of handles in message.
    uint32_t actual_bytes, actual_handles;
    status = vfd_it->second->AvailableForRead(&actual_bytes, &actual_handles);
    if (status != ZX_OK) {
      if (status != ZX_ERR_PEER_CLOSED) {
        FX_LOGS(ERROR) << "Failed to read size of message: " << status;
        break;
      }

      // Silently ignore error and skip read.
      it->second &= ~__ZX_OBJECT_READABLE;
    }

    if (it->second & __ZX_OBJECT_READABLE) {
      VirtioChain chain;
      VirtioDescriptor desc;
      if (!AcquireWritableDescriptor(in_queue(), &chain, &desc)) {
        break;
      }
      // Total message size is NEW commands for each handle, the RECV header,
      // the ID of each VFD and the data.
      size_t message_size =
          sizeof(virtio_wl_ctrl_vfd_recv_t) + sizeof(uint32_t) * actual_handles + actual_bytes;
      if (desc.len < message_size) {
        FX_LOGS(ERROR) << "Descriptor is too small for message";
        break;
      }
      *chain.Used() = message_size;

      // Build RECV command for the message.
      auto header = reinterpret_cast<virtio_wl_ctrl_vfd_recv_t*>(desc.addr);
      header->hdr.type = VIRTIO_WL_CMD_VFD_RECV;
      header->hdr.flags = 0;
      header->vfd_id = it->first;
      header->vfd_count = actual_handles;
      auto vfd_ids = reinterpret_cast<uint32_t*>(header + 1);

      // Retrieve handles and read data into queue.
      zx_handle_info_t handle_infos[ZX_CHANNEL_MAX_MSG_HANDLES];
      status = vfd_it->second->Read(vfd_ids + actual_handles, handle_infos, actual_bytes,
                                    actual_handles, &actual_bytes, &actual_handles);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to read message: " << status;
        break;
      }

      // If we have handles, we need to first send NEW_VFD commands for each.
      // In this case we queue up the list of the handles that we need to
      // create VFDs for. Associate the RECV command with the last one so that
      // we don't return that chain until we've finished creating all the VFDs.
      for (uint32_t i = 0; i < actual_handles; ++i) {
        uint32_t vfd_id = next_vfd_id_++;
        vfd_ids[i] = vfd_id;
        PendingVfd pending_vfd = {
            .handle_info = handle_infos[i],
            .vfd_id = vfd_id,
        };
        if (i == actual_handles - 1) {
          pending_vfd.payload = std::move(chain);
        }
        pending_vfds_.push_back(std::move(pending_vfd));
      }

      CreatePendingVfds();

      if (chain.IsValid()) {
        chain.Return();
      }
      it->second &= ~__ZX_OBJECT_READABLE;
    }

    // Remove VFD from ready set and begin another wait if all signals have
    // been handled.
    if (!it->second) {
      ready_vfds_.erase(it);
      status = vfd_it->second->BeginWaitOnData();
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to begin waiting on VFD: " << status;
      }
    }
  }
}

bool VirtioWl::CreatePendingVfds() {
  TRACE_DURATION("machina", "VirtioWl::CreatePendingVfds");
  // Consume handles by creating a VFD for each handle.
  for (auto it = pending_vfds_.begin(); it != pending_vfds_.end(); it = pending_vfds_.erase(it)) {
    VirtioChain chain;
    VirtioDescriptor desc;
    if (!AcquireWritableDescriptor(in_queue(), &chain, &desc)) {
      return false;
    }
    auto new_vfd_cmd = reinterpret_cast<virtio_wl_ctrl_vfd_new_t*>(desc.addr);
    auto vfd_id = it->vfd_id;
    new_vfd_cmd->vfd_id = vfd_id;

    // Determine flags based on handle rights.
    new_vfd_cmd->flags = 0;
    if (it->handle_info.rights & ZX_RIGHT_READ) {
      new_vfd_cmd->flags |= VIRTIO_WL_VFD_READ;
    }
    if (it->handle_info.rights & ZX_RIGHT_WRITE) {
      new_vfd_cmd->flags |= VIRTIO_WL_VFD_WRITE;
    }

    switch (it->handle_info.type) {
      case ZX_OBJ_TYPE_VMO: {
        uint32_t map_flags = 0;
        if (it->handle_info.rights & ZX_RIGHT_READ) {
          map_flags |= ZX_VM_PERM_READ;
        }
        if (it->handle_info.rights & ZX_RIGHT_WRITE) {
          map_flags |= ZX_VM_PERM_WRITE;
        }
        std::unique_ptr<Memory> vfd =
            Memory::Create(zx::vmo(it->handle_info.handle), &vmar_, map_flags);
        if (!vfd) {
          FX_LOGS(ERROR) << "Failed to create memory instance for VMO";
          break;
        }
        new_vfd_cmd->hdr.type = VIRTIO_WL_CMD_VFD_NEW;
        new_vfd_cmd->hdr.flags = 0;
        new_vfd_cmd->pfn = vfd->addr() / PAGE_SIZE;
        new_vfd_cmd->size = vfd->size();
        vfds_[vfd_id] = std::move(vfd);
      } break;
      case ZX_OBJ_TYPE_SOCKET: {
        auto vfd = std::make_unique<Pipe>(
            zx::socket(it->handle_info.handle), zx::socket(),
            [this, vfd_id](async_dispatcher_t* dispatcher, async::Wait* wait, zx_status_t status,
                           const zx_packet_signal_t* signal) {
              OnDataAvailable(vfd_id, wait, status, signal);
            },
            fit::bind_member(this, &VirtioWl::OnCanWrite));
        if (!vfd) {
          FX_LOGS(ERROR) << "Failed to create pipe instance for socket";
          break;
        }
        zx_status_t status = vfd->BeginWaitOnData();
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "Failed to begin waiting on pipe: " << status;
          break;
        }
        new_vfd_cmd->hdr.type = VIRTIO_WL_CMD_VFD_NEW_PIPE;
        new_vfd_cmd->hdr.flags = 0;
        vfds_[vfd_id] = std::move(vfd);
      } break;
      default:
        FX_LOGS(ERROR) << "Invalid handle type";
        zx_handle_close(it->handle_info.handle);
        break;
    }

    *chain.Used() = sizeof(*new_vfd_cmd);
    chain.Return();
    if (it->payload.IsValid()) {
      it->payload.Return();
    }
  }
  return true;
}

bool VirtioWl::AcquireWritableDescriptor(VirtioQueue* queue, VirtioChain* chain,
                                         VirtioDescriptor* descriptor) {
  return queue->NextChain(chain) && chain->NextDescriptor(descriptor) && descriptor->writable;
}

int main(int argc, char** argv) {
  syslog::SetTags({"virtio_wl"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  VirtioWl virtio_wl(context.get());
  return loop.Run();
}
