// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pipe.h"

#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/auto_lock.h>
#include <fuchsia/hardware/goldfish/pipe/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/bti.h>

namespace goldfish {
namespace {

constexpr size_t DEFAULT_BUFFER_SIZE = 8192;

constexpr zx_signals_t SIGNALS =
    fuchsia_hardware_goldfish_pipe_SIGNAL_READABLE | fuchsia_hardware_goldfish_pipe_SIGNAL_WRITABLE;

constexpr uint32_t kConcurrencyCap = 64;

}  // namespace

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

const fuchsia_hardware_goldfish_pipe_Pipe_ops_t Pipe::kOps = {
    fidl::Binder<Pipe>::BindMember<&Pipe::SetBufferSize>,
    fidl::Binder<Pipe>::BindMember<&Pipe::SetEvent>,
    fidl::Binder<Pipe>::BindMember<&Pipe::GetBuffer>,
    fidl::Binder<Pipe>::BindMember<&Pipe::Read>,
    fidl::Binder<Pipe>::BindMember<&Pipe::Write>,
    fidl::Binder<Pipe>::BindMember<&Pipe::Call>,
};

Pipe::Pipe(zx_device_t* parent) : FidlServer("GoldfishPipe", kConcurrencyCap), pipe_(parent) {}

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
}

void Pipe::Init() {
  fbl::AutoLock lock(&lock_);

  if (!pipe_.is_valid()) {
    FailAsync(ZX_ERR_BAD_STATE, "Pipe::Pipe() no pipe protocol");
    return;
  }

  zx_status_t status = pipe_.GetBti(&bti_);
  if (status != ZX_OK) {
    FailAsync(status, "Pipe::Pipe() GetBti failed");
    return;
  }

  status = SetBufferSizeLocked(DEFAULT_BUFFER_SIZE);
  if (status != ZX_OK) {
    FailAsync(status, "Pipe::Pipe() failed to set initial buffer size");
    return;
  }

  status = zx::event::create(0, &event_);
  if (status != ZX_OK) {
    FailAsync(status, "Pipe::Pipe() failed to create event");
    return;
  }
  status = event_.signal(0, SIGNALS);
  ZX_ASSERT(status == ZX_OK);

  zx::vmo vmo;
  goldfish_pipe_signal_value_t signal_cb = {Pipe::OnSignal, this};
  status = pipe_.Create(&signal_cb, &id_, &vmo);
  if (status != ZX_OK) {
    FailAsync(status, "Pipe::Pipe() failed to create pipe");
    return;
  }

  status = cmd_buffer_.InitVmo(bti_.get(), vmo.get(), 0, IO_BUFFER_RW);
  if (status != ZX_OK) {
    FailAsync(status, "Pipe::Pipe() io_buffer_init_vmo failed");
    return;
  }

  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd = PIPE_CMD_CODE_OPEN;
  buffer->status = PIPE_ERROR_INVAL;

  pipe_.Open(id_);
  if (buffer->status) {
    FailAsync(ZX_ERR_INTERNAL, "Pipe::Pipe() failed to open pipe");
    cmd_buffer_.release();
  }
}

zx_status_t Pipe::SetBufferSize(uint64_t size, fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Pipe::SetBufferSize", "size", size);

  BindingType::Txn::RecognizeTxn(txn);

  fbl::AutoLock lock(&lock_);

  zx_status_t status = SetBufferSizeLocked(size);
  if (status != ZX_OK) {
    LogError("Pipe::SetBufferSize() failed to create buffer: %lu", size);
    return status;
  }

  return fuchsia_hardware_goldfish_pipe_PipeSetBufferSize_reply(txn, status);
}

zx_status_t Pipe::SetEvent(zx_handle_t event_handle) {
  TRACE_DURATION("gfx", "Pipe::SetEvent");

  zx::event event(event_handle);
  if (!event.is_valid()) {
    LogError("Pipe::SetEvent() invalid event");
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);

  zx_handle_t observed = 0;
  zx_status_t status = event_.wait_one(SIGNALS, zx::time::infinite_past(), &observed);
  if (status != ZX_OK) {
    LogError("Pipe::SetEvent() failed to transfer observed signals: %d", status);
    return status;
  }

  event_ = std::move(event);
  status = event_.signal(SIGNALS, observed & SIGNALS);
  ZX_ASSERT(status == ZX_OK);
  return ZX_OK;
}

zx_status_t Pipe::GetBuffer(fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Pipe::GetBuffer");

  BindingType::Txn::RecognizeTxn(txn);

  fbl::AutoLock lock(&lock_);

  zx::vmo vmo;
  zx_status_t status = buffer_.vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  if (status != ZX_OK) {
    LogError("Pipe::GetBuffer() zx_vmo_duplicate failed: %d", status);
    return status;
  }

  return fuchsia_hardware_goldfish_pipe_PipeGetBuffer_reply(txn, ZX_OK, vmo.release());
}

zx_status_t Pipe::Read(size_t count, zx_off_t offset, fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Pipe::Read", "count", count);

  BindingType::Txn::RecognizeTxn(txn);

  fbl::AutoLock lock(&lock_);

  if ((offset + count) > buffer_.size || (offset + count) < offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t actual;
  zx_status_t status = TransferLocked(PIPE_CMD_CODE_READ, PIPE_CMD_CODE_WAKE_ON_READ,
                                      fuchsia_hardware_goldfish_pipe_SIGNAL_READABLE,
                                      buffer_.phys + offset, count, 0, 0, &actual);
  return fuchsia_hardware_goldfish_pipe_PipeRead_reply(txn, status, actual);
}

zx_status_t Pipe::Write(size_t count, zx_off_t offset, fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Pipe::Write", "count", count);

  BindingType::Txn::RecognizeTxn(txn);

  fbl::AutoLock lock(&lock_);

  if ((offset + count) > buffer_.size || (offset + count) < offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t actual;
  zx_status_t status = TransferLocked(PIPE_CMD_CODE_WRITE, PIPE_CMD_CODE_WAKE_ON_WRITE,
                                      fuchsia_hardware_goldfish_pipe_SIGNAL_WRITABLE,
                                      buffer_.phys + offset, count, 0, 0, &actual);
  return fuchsia_hardware_goldfish_pipe_PipeWrite_reply(txn, status, actual);
}

zx_status_t Pipe::Call(size_t count, zx_off_t offset, size_t read_count, zx_off_t read_offset,
                       fidl_txn_t* txn) {
  TRACE_DURATION("gfx", "Pipe::Call", "count", count, "read_count", read_count);

  BindingType::Txn::RecognizeTxn(txn);

  fbl::AutoLock lock(&lock_);

  if ((offset + count) > buffer_.size || (offset + count) < offset) {
    return ZX_ERR_INVALID_ARGS;
  }
  if ((read_offset + read_count) > buffer_.size || (read_offset + read_count) < read_offset) {
    return ZX_ERR_INVALID_ARGS;
  }

  size_t remaining = count;
  size_t remaining_read = read_count;

  int32_t cmd = PIPE_CMD_CODE_WRITE;
  zx_paddr_t read_paddr = 0;
  if (read_count) {
    cmd = PIPE_CMD_CODE_CALL;
    read_paddr = buffer_.phys + read_offset;
  }

  // Blocking write. This should always make progress or fail.
  while (remaining) {
    size_t actual;
    zx_status_t status = TransferLocked(
        cmd, PIPE_CMD_CODE_WAKE_ON_WRITE, fuchsia_hardware_goldfish_pipe_SIGNAL_WRITABLE,
        buffer_.phys + offset, remaining, read_paddr, read_count, &actual);
    if (status == ZX_OK) {
      // Calculate bytes written and bytes read. Adjust counts and offsets accordingly.
      size_t actual_write = std::min(actual, remaining);
      size_t actual_read = actual - actual_write;
      remaining -= actual_write;
      offset += actual_write;
      remaining_read -= actual_read;
      read_offset += actual_read;
      continue;
    }
    if (status != ZX_ERR_SHOULD_WAIT) {
      return fuchsia_hardware_goldfish_pipe_PipeCall_reply(txn, status, 0);
    }
    signal_cvar_.Wait(&lock_);
  }

  // Non-blocking read if no data has been read yet.
  zx_status_t status = ZX_OK;
  if (read_count && remaining_read == read_count) {
    size_t actual = 0;
    status = TransferLocked(PIPE_CMD_CODE_READ, PIPE_CMD_CODE_WAKE_ON_READ,
                            fuchsia_hardware_goldfish_pipe_SIGNAL_READABLE,
                            buffer_.phys + read_offset, remaining_read, 0, 0, &actual);
    if (status == ZX_OK) {
      remaining_read -= actual;
    }
  }
  size_t actual_read = read_count - remaining_read;
  return fuchsia_hardware_goldfish_pipe_PipeCall_reply(txn, status, actual_read);
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
    LogError("Pipe::Transfer() transfer failed: %d", buffer->status);
    return ZX_ERR_INTERNAL;
  }

  // PIPE_ERROR_AGAIN means that we need to wait until pipe is
  // readable/writable before we can perform another transfer command.
  // Clear event_ READABLE/WRITABLE bits and request an interrupt that
  // will indicate that the pipe is again readable/writable.
  event_.signal(state_clr, 0);

  buffer->id = id_;
  buffer->cmd = wake_cmd;
  buffer->status = PIPE_ERROR_INVAL;
  pipe_.Exec(id_);
  if (buffer->status) {
    LogError("Pipe::Transfer() failed to request interrupt: %d", buffer->status);
    return ZX_ERR_INTERNAL;
  }

  return ZX_ERR_SHOULD_WAIT;
}

zx_status_t Pipe::SetBufferSizeLocked(size_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create_contiguous(bti_, size, 0, &vmo);
  if (status != ZX_OK) {
    LogError("Pipe::CreateBuffer() zx_vmo_create_contiguous failed %d size: %zu", status, size);
    return status;
  }

  zx_paddr_t phys;
  zx::pmt pmt;
  // We leave pinned continuously, since buffer is expected to be used frequently.
  status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, vmo, 0, size, &phys, 1, &pmt);
  if (status != ZX_OK) {
    LogError("Pipe::CreateBuffer() zx_bti_pin failed %d size: %zu", status, size);
    return status;
  }

  buffer_.vmo = std::move(vmo);
  buffer_.pmt = std::move(pmt);
  buffer_.size = size;
  buffer_.phys = phys;
  return ZX_OK;
}

// static
void Pipe::OnSignal(void* ctx, int32_t flags) {
  TRACE_DURATION("gfx", "Pipe::OnSignal", "flags", flags);

  zx_signals_t state_set = 0;
  if (flags & PIPE_WAKE_FLAG_CLOSED) {
    state_set |= fuchsia_hardware_goldfish_pipe_SIGNAL_HANGUP;
  }
  if (flags & PIPE_WAKE_FLAG_READ) {
    state_set |= fuchsia_hardware_goldfish_pipe_SIGNAL_READABLE;
  }
  if (flags & PIPE_WAKE_FLAG_WRITE) {
    state_set |= fuchsia_hardware_goldfish_pipe_SIGNAL_WRITABLE;
  }

  auto pipe = static_cast<Pipe*>(ctx);

  fbl::AutoLock lock(&pipe->lock_);
  // The event_ signal is for client code, while the signal_cvar_ is for this class.
  pipe->event_.signal(0, state_set);
  pipe->signal_cvar_.Signal();
}

}  // namespace goldfish
