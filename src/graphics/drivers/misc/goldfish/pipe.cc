// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/drivers/misc/goldfish/pipe.h"

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/trace/event.h>
#include <lib/zx/bti.h>

#include <fbl/auto_lock.h>

namespace goldfish {
namespace {

#define FAIL_ASYNC(epitaph, ...) FailAsync(epitaph, __FILE__, __LINE__, __VA_ARGS__)

static const char* kTag = "GoldfishPipe";

constexpr size_t DEFAULT_BUFFER_SIZE = 8192;

constexpr zx_signals_t SIGNALS = fuchsia_hardware_goldfish::wire::kSignalReadable |
                                 fuchsia_hardware_goldfish::wire::kSignalWritable;

}  // namespace

Pipe::Pipe(zx_device_t* parent, async_dispatcher_t* dispatcher, OnBindFn on_bind,
           OnCloseFn on_close)
    : on_bind_(std::move(on_bind)),
      on_close_(std::move(on_close)),
      dispatcher_(dispatcher),
      pipe_(parent) {}

Pipe::~Pipe() {
  fbl::AutoLock lock(&lock_);
  if (id_) {
    if (cmd_buffer_.is_valid()) {
      auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
      buffer->id = id_;
      buffer->cmd = PIPE_CMD_CODE_CLOSE;
      buffer->status = PIPE_ERROR_INVAL;

      pipe_.Exec(id_);
      ZX_DEBUG_ASSERT(!buffer->status);
    }
    pipe_.Destroy(id_);
  }

  if (binding_ref_) {
    binding_ref_->Unbind();
  }
}

void Pipe::Init() {
  fbl::AutoLock lock(&lock_);

  if (!pipe_.is_valid()) {
    FAIL_ASYNC(ZX_ERR_BAD_STATE, "[%s] Pipe::Pipe() no pipe protocol", kTag);
    return;
  }

  zx_status_t status = pipe_.GetBti(&bti_);
  if (status != ZX_OK) {
    FAIL_ASYNC(status, "[%s] Pipe::Pipe() GetBti failed", kTag);
    return;
  }

  status = SetBufferSizeLocked(DEFAULT_BUFFER_SIZE);
  if (status != ZX_OK) {
    FAIL_ASYNC(status, "[%s] Pipe::Pipe() failed to set initial buffer size", kTag);
    return;
  }

  zx::event event;
  status = zx::event::create(0, &event);
  if (status != ZX_OK) {
    FAIL_ASYNC(status, "[%s] Pipe::Pipe() failed to create event", kTag);
    return;
  }
  status = event.signal(0, SIGNALS);
  ZX_ASSERT(status == ZX_OK);

  zx::vmo vmo;
  status = pipe_.Create(&id_, &vmo);
  if (status != ZX_OK) {
    FAIL_ASYNC(status, "[%s] Pipe::Pipe() failed to create pipe", kTag);
    return;
  }
  status = pipe_.SetEvent(id_, std::move(event));
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Pipe::Pipe() failed to set event: %d", kTag, status);
    return;
  }

  status = cmd_buffer_.InitVmo(bti_.get(), vmo.get(), 0, IO_BUFFER_RW);
  if (status != ZX_OK) {
    FAIL_ASYNC(status, "[%s] Pipe::Pipe() io_buffer_init_vmo failed", kTag);
    return;
  }

  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd = PIPE_CMD_CODE_OPEN;
  buffer->status = PIPE_ERROR_INVAL;

  pipe_.Open(id_);
  if (buffer->status) {
    FAIL_ASYNC(ZX_ERR_INTERNAL, "[%s] Pipe::Pipe() failed to open pipe", kTag);
    cmd_buffer_.release();
  }
}

void Pipe::Bind(fidl::ServerEnd<fuchsia_hardware_goldfish::Pipe> server_request) {
  using PipeProtocol = fuchsia_hardware_goldfish::Pipe;
  using PipeServer = fidl::WireServer<PipeProtocol>;
  auto on_unbound = [this](PipeServer*, fidl::UnbindInfo info, fidl::ServerEnd<PipeProtocol>) {
    switch (info.reason()) {
      case fidl::Reason::kUnbind:
      case fidl::Reason::kPeerClosed:
        // Client closed without errors. No-op.
        break;
      case fidl::Reason::kClose:
        // Client closed with epitaph.
        zxlogf(DEBUG, "[%s] Pipe closed with epitaph: %d\n", kTag, info.status());
        break;
      default:
        // handle pipe error.
        zxlogf(ERROR, "[%s] Pipe error: %s\n", kTag, info.FormatDescription().c_str());
    }
    if (on_close_) {
      on_close_(this);
    }
  };

  auto binding =
      fidl::BindServer(dispatcher_, std::move(server_request), this, std::move(on_unbound));
  binding_ref_ =
      std::make_unique<fidl::ServerBindingRef<fuchsia_hardware_goldfish::Pipe>>(std::move(binding));
}

void Pipe::SetBufferSize(SetBufferSizeRequestView request,
                         SetBufferSizeCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "Pipe::SetBufferSize", "size", request->size);

  fbl::AutoLock lock(&lock_);

  zx_status_t status = SetBufferSizeLocked(request->size);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Pipe::SetBufferSize() failed to create buffer: %lu", kTag, request->size);
    completer.Close(status);
  } else {
    completer.Reply(status);
  }
}

void Pipe::SetEvent(SetEventRequestView request, SetEventCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "Pipe::SetEvent");

  if (!request->event.is_valid()) {
    zxlogf(ERROR, "[%s] Pipe::SetEvent() invalid event", kTag);
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  fbl::AutoLock lock(&lock_);

  zx_status_t status = pipe_.SetEvent(id_, std::move(request->event));
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] SetEvent failed: %d", kTag, status);
    completer.Close(ZX_ERR_INTERNAL);
    return;
  }

  completer.Close(ZX_OK);
}

void Pipe::GetBuffer(GetBufferRequestView request, GetBufferCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "Pipe::GetBuffer");

  fbl::AutoLock lock(&lock_);

  zx::vmo vmo;
  zx_status_t status = buffer_.vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Pipe::GetBuffer() zx_vmo_duplicate failed: %d", kTag, status);
    completer.Close(status);
  } else {
    completer.Reply(ZX_OK, std::move(vmo));
  }
}

void Pipe::Read(ReadRequestView request, ReadCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "Pipe::Read", "count", request->count);

  fbl::AutoLock lock(&lock_);

  if ((request->offset + request->count) > buffer_.size ||
      (request->offset + request->count) < request->offset) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  size_t actual;
  zx_status_t status =
      TransferLocked(PIPE_CMD_CODE_READ, PIPE_CMD_CODE_WAKE_ON_READ,
                     fuchsia_hardware_goldfish::wire::kSignalReadable,
                     buffer_.phys + request->offset, request->count, 0, 0, &actual);
  completer.Reply(status, actual);
}

void Pipe::Write(WriteRequestView request, WriteCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "Pipe::Write", "count", request->count);

  fbl::AutoLock lock(&lock_);

  if ((request->offset + request->count) > buffer_.size ||
      (request->offset + request->count) < request->offset) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  size_t actual;
  zx_status_t status =
      TransferLocked(PIPE_CMD_CODE_WRITE, PIPE_CMD_CODE_WAKE_ON_WRITE,
                     fuchsia_hardware_goldfish::wire::kSignalWritable,
                     buffer_.phys + request->offset, request->count, 0, 0, &actual);
  completer.Reply(status, actual);
}

void Pipe::DoCall(DoCallRequestView request, DoCallCompleter::Sync& completer) {
  TRACE_DURATION("gfx", "Pipe::DoCall", "count", request->count, "read_count", request->read_count);

  fbl::AutoLock lock(&lock_);

  if ((request->offset + request->count) > buffer_.size ||
      (request->offset + request->count) < request->offset) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  if ((request->read_offset + request->read_count) > buffer_.size ||
      (request->read_offset + request->read_count) < request->read_offset) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  int32_t cmd = 0, wake_cmd = 0;
  uint32_t wake_signal = 0u;
  zx_paddr_t read_paddr = 0u, write_paddr = 0u;
  // Set write command, signal and offset.
  if (request->count) {
    cmd = request->read_count ? PIPE_CMD_CODE_CALL : PIPE_CMD_CODE_WRITE;
    wake_cmd = PIPE_CMD_CODE_WAKE_ON_WRITE;
    wake_signal = PIPE_CMD_CODE_WAKE_ON_WRITE;
    write_paddr = buffer_.phys + request->offset;
  }
  // Set read command, signal and offset.
  if (request->read_count) {
    cmd = request->count ? PIPE_CMD_CODE_CALL : PIPE_CMD_CODE_READ;
    wake_cmd = PIPE_CMD_CODE_WAKE_ON_READ;
    wake_signal = PIPE_CMD_CODE_WAKE_ON_READ;
    read_paddr = buffer_.phys + request->read_offset;
  }

  size_t actual = 0u;
  zx_status_t status = TransferLocked(cmd, wake_cmd, wake_signal, write_paddr, request->count,
                                      read_paddr, request->read_count, &actual);

  completer.Reply(status, actual);
}

// This function can be trusted to complete fairly quickly. It will cause a
// VM exit but that should never block for a significant amount of time.
zx_status_t Pipe::TransferLocked(int32_t cmd, int32_t wake_cmd, zx_signals_t state_clr,
                                 zx_paddr_t paddr, size_t count, zx_paddr_t read_paddr,
                                 size_t read_count, size_t* actual) {
  TRACE_DURATION("gfx", "Pipe::Transfer", "count", count, "read_count", read_count);

  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd = cmd;
  buffer->status = PIPE_ERROR_INVAL;
  buffer->rw_params.ptrs[0] = paddr;
  buffer->rw_params.sizes[0] = static_cast<uint32_t>(count);
  buffer->rw_params.ptrs[1] = read_paddr;
  buffer->rw_params.sizes[1] = static_cast<uint32_t>(read_count);
  buffer->rw_params.buffers_count = read_paddr ? 2 : 1;
  buffer->rw_params.consumed_size = 0;
  buffer->rw_params.read_index = 1;  // Read buffer is always second.
  pipe_.Exec(id_);

  // Positive consumed size always indicate a successful transfer.
  if (buffer->rw_params.consumed_size) {
    *actual = buffer->rw_params.consumed_size;
    return ZX_OK;
  }

  *actual = 0;
  // Early out if error is not because of back-pressure.
  if (buffer->status != PIPE_ERROR_AGAIN) {
    zxlogf(ERROR, "[%s] Pipe::Transfer() transfer failed: %d", kTag, buffer->status);
    return ZX_ERR_INTERNAL;
  }

  buffer->id = id_;
  buffer->cmd = wake_cmd;
  buffer->status = PIPE_ERROR_INVAL;
  pipe_.Exec(id_);
  if (buffer->status) {
    zxlogf(ERROR, "[%s] Pipe::Transfer() failed to request interrupt: %d", kTag, buffer->status);
    return ZX_ERR_INTERNAL;
  }

  return ZX_ERR_SHOULD_WAIT;
}

zx_status_t Pipe::SetBufferSizeLocked(size_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create_contiguous(bti_, size, 0, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Pipe::CreateBuffer() zx_vmo_create_contiguous failed %d size: %zu", kTag,
           status, size);
    return status;
  }

  zx_paddr_t phys;
  zx::pmt pmt;
  // We leave pinned continuously, since buffer is expected to be used frequently.
  status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, vmo, 0, size, &phys, 1, &pmt);
  if (status != ZX_OK) {
    zxlogf(ERROR, "[%s] Pipe::CreateBuffer() zx_bti_pin failed %d size: %zu", kTag, status, size);
    return status;
  }

  buffer_ = Buffer{.vmo = std::move(vmo), .pmt = std::move(pmt), .size = size, .phys = phys};
  return ZX_OK;
}

void Pipe::FailAsync(zx_status_t epitaph, const char* file, int line, const char* format, ...) {
  if (binding_ref_) {
    binding_ref_->Close(epitaph);
  }

  va_list args;
  va_start(args, format);
  zxlogvf(ERROR, file, line, format, args);
  va_end(args);
}

Pipe::Buffer& Pipe::Buffer::operator=(Pipe::Buffer&& other) noexcept {
  if (pmt.is_valid()) {
    pmt.unpin();
  }
  vmo = std::move(other.vmo);
  pmt = std::move(other.pmt);
  phys = other.phys;
  size = other.size;
  return *this;
}

Pipe::Buffer::~Buffer() {
  if (pmt.is_valid()) {
    pmt.unpin();
  }
}

}  // namespace goldfish
