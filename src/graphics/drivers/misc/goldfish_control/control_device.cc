// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "control_device.h"

#include <fuchsia/sysmem/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async-2/fidl_server.h>
#include <lib/fidl-async-2/simple_binding.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/event.h>
#include <zircon/syscalls.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "src/graphics/drivers/misc/goldfish_control/goldfish_control-bind.h"

namespace goldfish {
namespace {

const char* kTag = "goldfish-control";

const char* kPipeName = "pipe:opengles";

constexpr uint32_t kClientFlags = 0;

constexpr uint32_t VULKAN_ONLY = 1;

struct CreateColorBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint32_t internalformat;
};
constexpr uint32_t kOP_rcCreateColorBuffer = 10012;
constexpr uint32_t kSize_rcCreateColorBuffer = 20;

struct CloseColorBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
};
constexpr uint32_t kOP_rcCloseColorBuffer = 10014;
constexpr uint32_t kSize_rcCloseColorBuffer = 12;

struct SetColorBufferVulkanModeCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
  uint32_t mode;
};
constexpr uint32_t kOP_rcSetColorBufferVulkanMode = 10045;
constexpr uint32_t kSize_rcSetColorBufferVulkanMode = 16;

zx_koid_t GetKoidForVmo(const zx::vmo& vmo) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_object_get_info() failed - status: %d\n", kTag, status);
    return ZX_KOID_INVALID;
  }
  return info.koid;
}

void vLog(bool is_error, const char* prefix1, const char* prefix2, const char* format,
          va_list args) {
  va_list args2;
  va_copy(args2, args);

  size_t buffer_bytes = vsnprintf(nullptr, 0, format, args) + 1;

  std::unique_ptr<char[]> buffer(new char[buffer_bytes]);

  size_t buffer_bytes_2 = vsnprintf(buffer.get(), buffer_bytes, format, args2) + 1;
  (void)buffer_bytes_2;
  // sanity check; should match so go ahead and assert that it does.
  ZX_DEBUG_ASSERT(buffer_bytes == buffer_bytes_2);
  va_end(args2);

  if (is_error) {
    zxlogf(ERROR, "[%s %s] %s\n", prefix1, prefix2, buffer.get());
  } else {
    zxlogf(TRACE, "[%s %s] %s\n", prefix1, prefix2, buffer.get());
  }
}

constexpr uint32_t kConcurrencyCap = 64;

// An instance of this class serves a Heap connection.
class Heap : public FidlServer<
                 Heap, SimpleBinding<Heap, fuchsia_sysmem_Heap_ops_t, fuchsia_sysmem_Heap_dispatch>,
                 vLog> {
 public:
  // Public for std::unique_ptr<Heap>:
  ~Heap() = default;

 private:
  friend class FidlServer;

  Heap(Control* control) : FidlServer("GoldfishHeap", kConcurrencyCap), control_(control) {}

  zx_status_t AllocateVmo(uint64_t size, fidl_txn* txn) {
    BindingType::Txn::RecognizeTxn(txn);

    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(size, 0, &vmo);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: zx::vmo::create() failed - size: %lu status: %d\n", kTag, size, status);
      return fuchsia_sysmem_HeapAllocateVmo_reply(txn, status, ZX_HANDLE_INVALID);
    }

    return fuchsia_sysmem_HeapAllocateVmo_reply(txn, ZX_OK, vmo.release());
  }

  zx_status_t CreateResource(zx_handle_t vmo_handle, fidl_txn* txn) {
    BindingType::Txn::RecognizeTxn(txn);

    zx::vmo vmo(vmo_handle);

    zx_koid_t id = GetKoidForVmo(vmo);
    if (id == ZX_KOID_INVALID) {
      return fuchsia_sysmem_HeapCreateResource_reply(txn, ZX_ERR_INVALID_ARGS, 0);
    }

    control_->RegisterColorBuffer(id);
    return fuchsia_sysmem_HeapCreateResource_reply(txn, ZX_OK, id);
  }

  zx_status_t DestroyResource(uint64_t id, fidl_txn* txn) {
    BindingType::Txn::RecognizeTxn(txn);

    control_->FreeColorBuffer(id);
    return fuchsia_sysmem_HeapDestroyResource_reply(txn);
  }

  static constexpr fuchsia_sysmem_Heap_ops_t kOps = {
      fidl::Binder<Heap>::BindMember<&Heap::AllocateVmo>,
      fidl::Binder<Heap>::BindMember<&Heap::CreateResource>,
      fidl::Binder<Heap>::BindMember<&Heap::DestroyResource>,
  };

  Control* const control_;
};

}  // namespace

// static
zx_status_t Control::Create(void* ctx, zx_device_t* device) {
  auto control = std::make_unique<Control>(device);

  zx_status_t status = control->Bind();
  if (status == ZX_OK) {
    // devmgr now owns device.
    __UNUSED auto* dev = control.release();
  }
  return status;
}

Control::Control(zx_device_t* parent)
    : ControlType(parent), pipe_(parent), heap_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  goldfish_control_protocol_t self{&goldfish_control_protocol_ops_, this};
  control_ = ddk::GoldfishControlProtocolClient(&self);
}

Control::~Control() {
  heap_loop_.Shutdown();
  if (id_) {
    fbl::AutoLock lock(&lock_);
    if (cmd_buffer_.is_valid()) {
      for (auto& buffer : color_buffers_) {
        CloseColorBufferLocked(buffer.second);
      }
      auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
      buffer->id = id_;
      buffer->cmd = PIPE_CMD_CODE_CLOSE;
      buffer->status = PIPE_ERROR_INVAL;

      pipe_.Exec(id_);
      ZX_DEBUG_ASSERT(!buffer->status);
    }
    pipe_.Destroy(id_);
  }
}

zx_status_t Control::Bind() {
  fbl::AutoLock lock(&lock_);

  if (!pipe_.is_valid()) {
    zxlogf(ERROR, "%s: no pipe protocol\n", kTag);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = pipe_.GetBti(&bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBti failed: %d\n", kTag, status);
    return status;
  }

  status = io_buffer_.Init(bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: io_buffer_init failed: %d\n", kTag, status);
    return status;
  }

  zx::vmo vmo;
  goldfish_pipe_signal_value_t signal_cb = {Control::OnSignal, this};
  status = pipe_.Create(&signal_cb, &id_, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Create failed: %d\n", kTag, status);
    return status;
  }

  status = cmd_buffer_.InitVmo(bti_.get(), vmo.get(), 0, IO_BUFFER_RW);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: io_buffer_init_vmo failed: %d\n", kTag, status);
    return status;
  }

  auto release_buffer =
      fbl::MakeAutoCall([this]() TA_NO_THREAD_SAFETY_ANALYSIS { cmd_buffer_.release(); });

  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd = PIPE_CMD_CODE_OPEN;
  buffer->status = PIPE_ERROR_INVAL;

  pipe_.Open(id_);
  if (buffer->status) {
    zxlogf(ERROR, "%s: Open failed: %d\n", kTag, buffer->status);
    return ZX_ERR_INTERNAL;
  }

  // Keep buffer after successful execution of OPEN command. This way
  // we'll send CLOSE later.
  release_buffer.cancel();

  size_t length = strlen(kPipeName) + 1;
  memcpy(io_buffer_.virt(), kPipeName, length);
  int32_t consumed_size = 0;
  int32_t result = WriteLocked(static_cast<uint32_t>(length), &consumed_size);
  if (result < 0) {
    zxlogf(ERROR, "%s: failed connecting to '%s' pipe: %d\n", kTag, kPipeName, result);
    return ZX_ERR_INTERNAL;
  }
  ZX_DEBUG_ASSERT(consumed_size == static_cast<int32_t>(length));

  memcpy(io_buffer_.virt(), &kClientFlags, sizeof(kClientFlags));
  WriteLocked(sizeof(kClientFlags));

  // We are now ready to serve goldfish heap allocations. Create a channel
  // and register client-end with sysmem.
  zx::channel heap_request, heap_connection;
  status = zx::channel::create(0, &heap_request, &heap_connection);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx::channel:create() failed: %d\n", kTag, status);
    return status;
  }
  status = pipe_.RegisterSysmemHeap(fuchsia_sysmem_HeapType_GOLDFISH_DEVICE_LOCAL,
                                    std::move(heap_connection));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to register heap: %d\n", kTag, status);
    return status;
  }

  // Start server thread. Heap server must be running on a seperate
  // thread as sysmem might be making synchronous allocation requests
  // from the main thread.
  heap_loop_.StartThread("goldfish_control_heap_thread");
  async::PostTask(heap_loop_.dispatcher(), [this, request = std::move(heap_request)]() mutable {
    // The Heap is channel-owned / self-owned.
    Heap::CreateChannelOwned(std::move(request), this);
  });

  return DdkAdd("goldfish-control", 0, nullptr, 0, ZX_PROTOCOL_GOLDFISH_CONTROL);
}

void Control::RegisterColorBuffer(zx_koid_t koid) {
  fbl::AutoLock lock(&lock_);
  color_buffers_[koid] = 0;
}

void Control::FreeColorBuffer(zx_koid_t koid) {
  fbl::AutoLock lock(&lock_);

  auto it = color_buffers_.find(koid);
  if (it == color_buffers_.end()) {
    zxlogf(ERROR, "%s: invalid key\n", kTag);
    return;
  }

  if (it->second) {
    CloseColorBufferLocked(it->second);
  }
  color_buffers_.erase(it);
}

zx_status_t Control::FidlCreateColorBuffer(zx_handle_t vmo_handle, uint32_t width, uint32_t height,
                                           uint32_t format, fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Control::FidlCreateColorBuffer", "width", width, "height", height);

  zx::vmo vmo(vmo_handle);
  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);

  auto it = color_buffers_.find(koid);
  if (it == color_buffers_.end()) {
    zxlogf(ERROR, "%s: invalid VMO\n", kTag);
    return ZX_ERR_INVALID_ARGS;
  }

  if (it->second) {
    return fuchsia_hardware_goldfish_ControlDeviceCreateColorBuffer_reply(txn,
                                                                          ZX_ERR_ALREADY_EXISTS);
  }

  uint32_t id;
  zx_status_t status = CreateColorBufferLocked(width, height, format, &id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create color buffer: %d\n", kTag, status);
    return status;
  }

  auto close_color_buffer =
      fbl::MakeAutoCall([this, id]() TA_NO_THREAD_SAFETY_ANALYSIS { CloseColorBufferLocked(id); });

  uint32_t result = 0;
  status = SetColorBufferVulkanModeLocked(id, VULKAN_ONLY, &result);
  if (status != ZX_OK || result) {
    zxlogf(ERROR, "%s: failed to set vulkan mode: %d %d\n", kTag, status, result);
    return status;
  }

  close_color_buffer.cancel();
  it->second = id;
  return fuchsia_hardware_goldfish_ControlDeviceCreateColorBuffer_reply(txn, ZX_OK);
}

zx_status_t Control::FidlGetColorBuffer(zx_handle_t vmo_handle, fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Control::FidlGetColorBuffer");

  zx::vmo vmo(vmo_handle);
  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);

  auto it = color_buffers_.find(koid);
  if (it == color_buffers_.end()) {
    return fuchsia_hardware_goldfish_ControlDeviceGetColorBuffer_reply(txn, ZX_ERR_INVALID_ARGS, 0);
  }

  return fuchsia_hardware_goldfish_ControlDeviceGetColorBuffer_reply(txn, ZX_OK, it->second);
}

void Control::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void Control::DdkRelease() { delete this; }

zx_status_t Control::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  using Binder = fidl::Binder<Control>;

  static const fuchsia_hardware_goldfish_ControlDevice_ops_t kOps = {
      .CreateColorBuffer = Binder::BindMember<&Control::FidlCreateColorBuffer>,
      .GetColorBuffer = Binder::BindMember<&Control::FidlGetColorBuffer>,
  };

  return fuchsia_hardware_goldfish_ControlDevice_dispatch(this, txn, msg, &kOps);
}

zx_status_t Control::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  fbl::AutoLock lock(&lock_);

  switch (proto_id) {
    case ZX_PROTOCOL_GOLDFISH_PIPE: {
      pipe_.GetProto(static_cast<goldfish_pipe_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    case ZX_PROTOCOL_GOLDFISH_CONTROL: {
      control_.GetProto(static_cast<goldfish_control_protocol_t*>(out_protocol));
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t Control::GoldfishControlGetColorBuffer(zx::vmo vmo, uint32_t* out_id) {
  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);

  auto it = color_buffers_.find(koid);
  if (it == color_buffers_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  *out_id = it->second;
  return ZX_OK;
}

void Control::OnSignal(void* ctx, int32_t flags) {
  TRACE_DURATION("gfx", "Control::OnSignal", "flags", flags);

  if (flags & (PIPE_WAKE_FLAG_READ | PIPE_WAKE_FLAG_CLOSED)) {
    static_cast<Control*>(ctx)->OnReadable();
  }
}

void Control::OnReadable() {
  TRACE_DURATION("gfx", "Control::OnReadable");

  fbl::AutoLock lock(&lock_);
  readable_cvar_.Signal();
}

int32_t Control::WriteLocked(uint32_t cmd_size, int32_t* consumed_size) {
  TRACE_DURATION("gfx", "Control::Write", "cmd_size", cmd_size);

  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd = PIPE_CMD_CODE_WRITE;
  buffer->status = PIPE_ERROR_INVAL;
  buffer->rw_params.ptrs[0] = io_buffer_.phys();
  buffer->rw_params.sizes[0] = cmd_size;
  buffer->rw_params.buffers_count = 1;
  buffer->rw_params.consumed_size = 0;
  pipe_.Exec(id_);
  *consumed_size = buffer->rw_params.consumed_size;
  return buffer->status;
}

void Control::WriteLocked(uint32_t cmd_size) {
  int32_t consumed_size;
  int32_t result = WriteLocked(cmd_size, &consumed_size);
  ZX_DEBUG_ASSERT(result >= 0);
  ZX_DEBUG_ASSERT(consumed_size == static_cast<int32_t>(cmd_size));
}

zx_status_t Control::ReadResultLocked(uint32_t* result) {
  TRACE_DURATION("gfx", "Control::ReadResult");

  while (true) {
    auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
    buffer->id = id_;
    buffer->cmd = PIPE_CMD_CODE_READ;
    buffer->status = PIPE_ERROR_INVAL;
    buffer->rw_params.ptrs[0] = io_buffer_.phys();
    buffer->rw_params.sizes[0] = sizeof(*result);
    buffer->rw_params.buffers_count = 1;
    buffer->rw_params.consumed_size = 0;
    pipe_.Exec(id_);

    // Positive consumed size always indicate a successful transfer.
    if (buffer->rw_params.consumed_size) {
      ZX_DEBUG_ASSERT(buffer->rw_params.consumed_size == sizeof(*result));
      *result = *static_cast<uint32_t*>(io_buffer_.virt());
      return ZX_OK;
    }

    // Early out if error is not because of back-pressure.
    if (buffer->status != PIPE_ERROR_AGAIN) {
      zxlogf(ERROR, "%s: reading result failed: %d\n", kTag, buffer->status);
      return ZX_ERR_INTERNAL;
    }

    buffer->id = id_;
    buffer->cmd = PIPE_CMD_CODE_WAKE_ON_READ;
    buffer->status = PIPE_ERROR_INVAL;
    pipe_.Exec(id_);
    ZX_DEBUG_ASSERT(!buffer->status);

    // Wait for pipe to become readable.
    readable_cvar_.Wait(&lock_);
  }
}

zx_status_t Control::ExecuteCommandLocked(uint32_t cmd_size, uint32_t* result) {
  TRACE_DURATION("gfx", "Control::ExecuteCommand", "cnd_size", cmd_size);

  WriteLocked(cmd_size);
  return ReadResultLocked(result);
}

zx_status_t Control::CreateColorBufferLocked(uint32_t width, uint32_t height, uint32_t format,
                                             uint32_t* id) {
  TRACE_DURATION("gfx", "Control::CreateColorBuffer", "width", width, "height", height);

  auto cmd = static_cast<CreateColorBufferCmd*>(io_buffer_.virt());
  cmd->op = kOP_rcCreateColorBuffer;
  cmd->size = kSize_rcCreateColorBuffer;
  cmd->width = width;
  cmd->height = height;
  cmd->internalformat = format;

  return ExecuteCommandLocked(kSize_rcCreateColorBuffer, id);
}

void Control::CloseColorBufferLocked(uint32_t id) {
  TRACE_DURATION("gfx", "Control::CloseColorBuffer", "id", id);

  auto cmd = static_cast<CloseColorBufferCmd*>(io_buffer_.virt());
  cmd->op = kOP_rcCloseColorBuffer;
  cmd->size = kSize_rcCloseColorBuffer;
  cmd->id = id;

  WriteLocked(kSize_rcCloseColorBuffer);
}

zx_status_t Control::SetColorBufferVulkanModeLocked(uint32_t id, uint32_t mode, uint32_t* result) {
  TRACE_DURATION("gfx", "Control::SetColorBufferVulkanMode", "id", id, "mode", mode);

  auto cmd = static_cast<SetColorBufferVulkanModeCmd*>(io_buffer_.virt());
  cmd->op = kOP_rcSetColorBufferVulkanMode;
  cmd->size = kSize_rcSetColorBufferVulkanMode;
  cmd->id = id;
  cmd->mode = mode;

  return ExecuteCommandLocked(kSize_rcSetColorBufferVulkanMode, result);
}

}  // namespace goldfish

static constexpr zx_driver_ops_t goldfish_control_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = goldfish::Control::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER(goldfish_control, goldfish_control_driver_ops, "zircon", "0.1");

// clang-format on
