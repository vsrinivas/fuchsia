// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish_control/control_device.h"

#include <zircon/syscalls.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <ddktl/fidl.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "src/graphics/drivers/misc/goldfish_control/heap.h"

namespace goldfish {
namespace {

const char* kTag = "goldfish-control";

const char* kPipeName = "pipe:opengles";

constexpr uint32_t kClientFlags = 0;

constexpr uint32_t VULKAN_ONLY = 1;

constexpr uint32_t kInvalidColorBuffer = 0U;

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

struct CreateBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t buffer_size;
};
constexpr uint32_t kOP_rcCreateBuffer = 10049;
constexpr uint32_t kSize_rcCreateBuffer = 12;

struct CloseBufferCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
};
constexpr uint32_t kOP_rcCloseBuffer = 10050;
constexpr uint32_t kSize_rcCloseBuffer = 12;

struct SetColorBufferVulkanMode2Cmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
  uint32_t mode;
  uint32_t memory_property;
};
constexpr uint32_t kOP_rcSetColorBufferVulkanMode2 = 10051;
constexpr uint32_t kSize_rcSetColorBufferVulkanMode2 = 20;

struct __attribute__((__packed__)) MapGpaToBufferHandleCmd {
  uint32_t op;
  uint32_t size;
  uint32_t id;
  uint64_t gpa;
};
constexpr uint32_t kOP_rcMapGpaToBufferHandle = 10052;
constexpr uint32_t kSize_rcMapGpaToBufferHandle = 20;

zx_koid_t GetKoidForVmo(const zx::vmo& vmo) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(vmo.get(), ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_object_get_info() failed - status: %d", kTag, status);
    return ZX_KOID_INVALID;
  }
  return info.koid;
}

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

Control::Control(zx_device_t* parent) : ControlType(parent), pipe_(parent) {
  goldfish_control_protocol_t self{&goldfish_control_protocol_ops_, this};
  control_ = ddk::GoldfishControlProtocolClient(&self);
}

Control::~Control() {
  if (id_) {
    fbl::AutoLock lock(&lock_);
    if (cmd_buffer_.is_valid()) {
      for (auto& buffer : buffer_handles_) {
        CloseBufferOrColorBufferLocked(buffer.second);
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
    zxlogf(ERROR, "%s: no pipe protocol", kTag);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = pipe_.GetBti(&bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBti failed: %d", kTag, status);
    return status;
  }

  status = io_buffer_.Init(bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: io_buffer_init failed: %d", kTag, status);
    return status;
  }

  zx::vmo vmo;
  goldfish_pipe_signal_value_t signal_cb = {Control::OnSignal, this};
  status = pipe_.Create(&signal_cb, &id_, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Create failed: %d", kTag, status);
    return status;
  }

  status = cmd_buffer_.InitVmo(bti_.get(), vmo.get(), 0, IO_BUFFER_RW);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: io_buffer_init_vmo failed: %d", kTag, status);
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
    zxlogf(ERROR, "%s: Open failed: %d", kTag, buffer->status);
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
    zxlogf(ERROR, "%s: failed connecting to '%s' pipe: %d", kTag, kPipeName, result);
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
    zxlogf(ERROR, "%s: zx::channel:create() failed: %d", kTag, status);
    return status;
  }
  status = pipe_.RegisterSysmemHeap(
      static_cast<uint64_t>(llcpp::fuchsia::sysmem::HeapType::GOLDFISH_DEVICE_LOCAL),
      std::move(heap_connection));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to register heap: %d", kTag, status);
    return status;
  }

  std::unique_ptr<Heap> heap = Heap::Create(this);
  Heap* heap_ptr = heap.get();
  heaps_.push_back(std::move(heap));
  heap_ptr->Bind(std::move(heap_request));

  return DdkAdd(ddk::DeviceAddArgs("goldfish-control").set_proto_id(ZX_PROTOCOL_GOLDFISH_CONTROL));
}

uint64_t Control::RegisterBufferHandle(const zx::vmo& vmo) {
  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    return static_cast<uint64_t>(ZX_KOID_INVALID);
  }

  fbl::AutoLock lock(&lock_);
  buffer_handles_[koid] = kInvalidColorBuffer;
  return static_cast<uint64_t>(koid);
}

void Control::FreeBufferHandle(uint64_t id) {
  fbl::AutoLock lock(&lock_);

  auto it = buffer_handles_.find(static_cast<zx_koid_t>(id));
  if (it == buffer_handles_.end()) {
    zxlogf(ERROR, "%s: invalid key", kTag);
    return;
  }

  if (it->second) {
    CloseBufferOrColorBufferLocked(it->second);
  }
  buffer_handle_types_.erase(it->second);
  buffer_handles_.erase(it);
}

void Control::CreateColorBuffer(zx::vmo vmo, uint32_t width, uint32_t height,
                                llcpp::fuchsia::hardware::goldfish::ColorBufferFormatType format,
                                CreateColorBufferCompleter::Sync completer) {
  TRACE_DURATION("gfx", "Control::FidlCreateColorBuffer", "width", width, "height", height);

  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&lock_);

  auto it = buffer_handles_.find(koid);
  if (it == buffer_handles_.end()) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (it->second != kInvalidColorBuffer) {
    completer.Reply(ZX_ERR_ALREADY_EXISTS);
    return;
  }

  uint32_t id;
  zx_status_t status = CreateColorBufferLocked(width, height, static_cast<uint32_t>(format), &id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create color buffer: %d", kTag, status);
    completer.Close(status);
    return;
  }

  auto close_color_buffer =
      fbl::MakeAutoCall([this, id]() TA_NO_THREAD_SAFETY_ANALYSIS { CloseColorBufferLocked(id); });

  uint32_t result = 0;
  status = SetColorBufferVulkanMode2Locked(id, VULKAN_ONLY, 0u /*memory_property*/, &result);
  if (status != ZX_OK || result) {
    zxlogf(ERROR, "%s: failed to set vulkan mode: %d %d", kTag, status, result);
    completer.Close(status);
    return;
  }

  close_color_buffer.cancel();
  it->second = id;
  buffer_handle_types_[id] = llcpp::fuchsia::hardware::goldfish::BufferHandleType::COLOR_BUFFER;
  completer.Reply(ZX_OK);
}

void Control::CreateColorBuffer2(
    zx::vmo vmo, llcpp::fuchsia::hardware::goldfish::CreateColorBuffer2Params create_params,
    CreateColorBuffer2Completer::Sync completer) {
  // Check argument validity.
  if (!create_params.has_width() || !create_params.has_height() || !create_params.has_format() ||
      !create_params.has_memory_property()) {
    zxlogf(ERROR, "%s: invalid arguments: width? %d height? %d format? %d memory property? %d\n",
           kTag, create_params.has_width(), create_params.has_height(), create_params.has_format(),
           create_params.has_memory_property());
    completer.Reply(ZX_ERR_INVALID_ARGS, -1);
    return;
  }
  if ((create_params.memory_property() &
       llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE) &&
      !create_params.has_physical_address()) {
    zxlogf(ERROR, "%s: invalid arguments: memory_property %d, no physical address\n", kTag,
           create_params.memory_property());
    completer.Reply(ZX_ERR_INVALID_ARGS, -1);
    return;
  }

  TRACE_DURATION("gfx", "Control::CreateColorBuffer2", "width", create_params.width(), "height",
                 create_params.height(), "format", static_cast<uint32_t>(create_params.format()),
                 "memory_property", create_params.memory_property());

  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&lock_);

  auto it = buffer_handles_.find(koid);
  if (it == buffer_handles_.end()) {
    completer.Reply(ZX_ERR_INVALID_ARGS, -1);
    return;
  }

  if (it->second != kInvalidColorBuffer) {
    completer.Reply(ZX_ERR_ALREADY_EXISTS, -1);
    return;
  }

  uint32_t id;
  zx_status_t status = CreateColorBufferLocked(create_params.width(), create_params.height(),
                                               static_cast<uint32_t>(create_params.format()), &id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create color buffer: %d", kTag, status);
    completer.Close(status);
    return;
  }

  auto close_color_buffer =
      fbl::MakeAutoCall([this, id]() TA_NO_THREAD_SAFETY_ANALYSIS { CloseColorBufferLocked(id); });

  uint32_t result = 0;
  status =
      SetColorBufferVulkanMode2Locked(id, VULKAN_ONLY, create_params.memory_property(), &result);
  if (status != ZX_OK || result) {
    zxlogf(ERROR, "%s: failed to set vulkan mode: %d %d", kTag, status, result);
    completer.Close(status);
    return;
  }

  int32_t hw_address_page_offset = -1;
  if (create_params.memory_property() &
      llcpp::fuchsia::hardware::goldfish::MEMORY_PROPERTY_HOST_VISIBLE) {
    uint32_t map_result = 0;
    status = MapGpaToBufferHandleLocked(id, create_params.physical_address(), &map_result);
    if (status != ZX_OK || map_result < 0) {
      zxlogf(ERROR, "%s: failed to map gpa to color buffer: %d %d", kTag, status, map_result);
      completer.Close(status);
      return;
    }

    hw_address_page_offset = map_result;
  }

  close_color_buffer.cancel();
  it->second = id;
  buffer_handle_types_[id] = llcpp::fuchsia::hardware::goldfish::BufferHandleType::COLOR_BUFFER;

  completer.Reply(ZX_OK, hw_address_page_offset);
}

void Control::CreateBuffer(zx::vmo vmo, uint32_t size, CreateBufferCompleter::Sync completer) {
  TRACE_DURATION("gfx", "Control::FidlCreateBuffer", "size", size);

  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&lock_);

  auto it = buffer_handles_.find(koid);
  if (it == buffer_handles_.end()) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (it->second != kInvalidColorBuffer) {
    completer.Reply(ZX_ERR_ALREADY_EXISTS);
    return;
  }

  uint32_t id;
  zx_status_t status = CreateBufferLocked(size, &id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to create buffer: %d", kTag, status);
    completer.Close(status);
    return;
  }

  it->second = id;
  buffer_handle_types_[id] = llcpp::fuchsia::hardware::goldfish::BufferHandleType::BUFFER;
  completer.Reply(ZX_OK);
}

void Control::GetColorBuffer(zx::vmo vmo, GetColorBufferCompleter::Sync completer) {
  TRACE_DURATION("gfx", "Control::FidlGetColorBuffer");

  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&lock_);

  auto it = buffer_handles_.find(koid);
  if (it == buffer_handles_.end()) {
    completer.Reply(ZX_ERR_INVALID_ARGS, 0);
    return;
  }

  if (it->second == kInvalidColorBuffer) {
    // Color buffer not created yet.
    completer.Reply(ZX_ERR_NOT_FOUND, 0);
    return;
  }

  completer.Reply(ZX_OK, it->second);
}

void Control::GetBufferHandle(zx::vmo vmo, GetBufferHandleCompleter::Sync completer) {
  TRACE_DURATION("gfx", "Control::FidlGetBufferHandle");

  zx_koid_t koid = GetKoidForVmo(vmo);
  if (koid == ZX_KOID_INVALID) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  uint32_t handle = kInvalidColorBuffer;
  auto handle_type = llcpp::fuchsia::hardware::goldfish::BufferHandleType::INVALID;

  fbl::AutoLock lock(&lock_);

  auto it = buffer_handles_.find(koid);
  if (it == buffer_handles_.end()) {
    completer.Reply(ZX_ERR_INVALID_ARGS, handle, handle_type);
    return;
  }

  handle = it->second;
  if (handle == kInvalidColorBuffer) {
    // Color buffer not created yet.
    completer.Reply(ZX_ERR_NOT_FOUND, handle, handle_type);
    return;
  }

  auto it_types = buffer_handle_types_.find(handle);
  if (it_types == buffer_handle_types_.end()) {
    // Color buffer type not registered yet.
    completer.Reply(ZX_ERR_NOT_FOUND, handle, handle_type);
    return;
  }

  handle_type = it_types->second;
  completer.Reply(ZX_OK, handle, handle_type);
}

void Control::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void Control::DdkRelease() { delete this; }

zx_status_t Control::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  llcpp::fuchsia::hardware::goldfish::ControlDevice::Dispatch(this, msg, &transaction);
  return transaction.Status();
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

  auto it = buffer_handles_.find(koid);
  if (it == buffer_handles_.end()) {
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
      zxlogf(ERROR, "%s: reading result failed: %d", kTag, buffer->status);
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

zx_status_t Control::CreateBufferLocked(uint32_t size, uint32_t* id) {
  TRACE_DURATION("gfx", "Control::CreateBuffer", "size", size);

  auto cmd = static_cast<CreateBufferCmd*>(io_buffer_.virt());
  cmd->op = kOP_rcCreateBuffer;
  cmd->size = kSize_rcCreateBuffer;
  cmd->buffer_size = size;

  return ExecuteCommandLocked(kSize_rcCreateBuffer, id);
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

void Control::CloseBufferOrColorBufferLocked(uint32_t id) {
  ZX_DEBUG_ASSERT(buffer_handle_types_.find(id) != buffer_handle_types_.end());
  auto buffer_type = buffer_handle_types_.at(id);
  switch (buffer_type) {
    case llcpp::fuchsia::hardware::goldfish::BufferHandleType::BUFFER:
      CloseBufferLocked(id);
      break;
    case llcpp::fuchsia::hardware::goldfish::BufferHandleType::COLOR_BUFFER:
      CloseColorBufferLocked(id);
      break;
    default:
      // Otherwise buffer/colorBuffer was not created. We don't need to do
      // anything.
      break;
  }
}

void Control::CloseColorBufferLocked(uint32_t id) {
  TRACE_DURATION("gfx", "Control::CloseColorBuffer", "id", id);

  auto cmd = static_cast<CloseColorBufferCmd*>(io_buffer_.virt());
  cmd->op = kOP_rcCloseColorBuffer;
  cmd->size = kSize_rcCloseColorBuffer;
  cmd->id = id;

  WriteLocked(kSize_rcCloseColorBuffer);
}

void Control::CloseBufferLocked(uint32_t id) {
  TRACE_DURATION("gfx", "Control::CloseBuffer", "id", id);

  auto cmd = static_cast<CloseBufferCmd*>(io_buffer_.virt());
  cmd->op = kOP_rcCloseBuffer;
  cmd->size = kSize_rcCloseBuffer;
  cmd->id = id;

  WriteLocked(kSize_rcCloseBuffer);
}

zx_status_t Control::SetColorBufferVulkanMode2Locked(uint32_t id, uint32_t mode,
                                                     uint32_t memory_property, uint32_t* result) {
  TRACE_DURATION("gfx", "Control::SetColorBufferVulkanMode2Locked", "id", id, "mode", mode,
                 "memory_property", memory_property);

  auto cmd = static_cast<SetColorBufferVulkanMode2Cmd*>(io_buffer_.virt());
  cmd->op = kOP_rcSetColorBufferVulkanMode2;
  cmd->size = kSize_rcSetColorBufferVulkanMode2;
  cmd->id = id;
  cmd->mode = mode;
  cmd->memory_property = memory_property;

  return ExecuteCommandLocked(kSize_rcSetColorBufferVulkanMode2, result);
}

zx_status_t Control::MapGpaToBufferHandleLocked(uint32_t id, uint64_t gpa, uint32_t* result) {
  TRACE_DURATION("gfx", "Control::MapGpaToBufferHandleLocked", "id", id, "gpa", gpa);

  auto cmd = static_cast<MapGpaToBufferHandleCmd*>(io_buffer_.virt());
  cmd->op = kOP_rcMapGpaToBufferHandle;
  cmd->size = kSize_rcMapGpaToBufferHandle;
  cmd->id = id;
  cmd->gpa = gpa;

  return ExecuteCommandLocked(kSize_rcMapGpaToBufferHandle, result);
}

void Control::RemoveHeap(Heap* heap) {
  fbl::AutoLock lock(&lock_);
  heaps_.erase(*heap);
}

}  // namespace goldfish

static constexpr zx_driver_ops_t goldfish_control_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = goldfish::Control::Create;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(goldfish_control, goldfish_control_driver_ops, "zircon",
                    "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_GOLDFISH_PIPE),
ZIRCON_DRIVER_END(goldfish_control)
    // clang-format on
