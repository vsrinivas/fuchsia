// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <lib/fit/result.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>

#include <cstdlib>
#include <cstring>

#include <fbl/auto_lock.h>

namespace goldfish {

PipeIo::PipeIo(const ddk::GoldfishPipeProtocolClient* pipe, const char* pipe_name) : pipe_(pipe) {
  auto status = Init(pipe_name);
  if (status == ZX_OK) {
    valid_ = true;
  }
}

zx_status_t PipeIo::SetupPipe() {
  fbl::AutoLock lock(&lock_);

  if (!pipe_->is_valid()) {
    zxlogf(ERROR, "%s: no pipe protocol", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = pipe_->GetBti(&bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBti failed: %d", __func__, status);
    return status;
  }

  status = io_buffer_.Init(bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  io_buffer_size_ = io_buffer_.size();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Init IO buffer failed: %d", __func__, status);
    return status;
  }

  ZX_DEBUG_ASSERT(!pipe_event_.is_valid());
  status = zx::event::create(0u, &pipe_event_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_event_create failed: %d", __func__, status);
    return status;
  }

  zx::event pipe_event_dup;
  status = pipe_event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &pipe_event_dup);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_handle_duplicate failed: %d", __func__, status);
    return status;
  }

  zx::vmo vmo;
  status = pipe_->Create(&id_, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Create pipe failed: %d", __func__, status);
    return status;
  }
  status = pipe_->SetEvent(id_, std::move(pipe_event_dup));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: SetEvent failed: %d", __func__, status);
    return status;
  }

  status = cmd_buffer_.InitVmo(bti_.get(), vmo.get(), 0, IO_BUFFER_RW);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: InitVmo failed: %d", __func__, status);
    return status;
  }

  auto release_buffer =
      fit::defer([this]() TA_NO_THREAD_SAFETY_ANALYSIS { cmd_buffer_.release(); });

  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd = PIPE_CMD_CODE_OPEN;
  buffer->status = PIPE_ERROR_INVAL;

  pipe_->Open(id_);
  if (buffer->status) {
    zxlogf(ERROR, "%s: Open failed: %d", __func__, buffer->status);
    return ZX_ERR_INTERNAL;
  }

  // Keep buffer after successful execution of OPEN command. This way
  // we'll send CLOSE later.
  release_buffer.cancel();

  return ZX_OK;
}

zx_status_t PipeIo::Init(const char* pipe_name) {
  zx_status_t status = SetupPipe();
  if (status != ZX_OK) {
    return status;
  }

  size_t length = strlen(pipe_name) + 1;
  std::vector<uint8_t> payload(length, 0u);
  std::copy(pipe_name, pipe_name + length, payload.begin());

  auto write_status = Write(payload);
  if (write_status != ZX_OK) {
    return write_status;
  }

  valid_ = true;
  return ZX_OK;
}

PipeIo::~PipeIo() {
  fbl::AutoLock lock(&lock_);
  if (id_) {
    if (cmd_buffer_.is_valid()) {
      auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
      buffer->id = id_;
      buffer->cmd = PIPE_CMD_CODE_CLOSE;
      buffer->status = PIPE_ERROR_INVAL;

      pipe_->Exec(id_);
      ZX_DEBUG_ASSERT(!buffer->status);
    }
    pipe_->Destroy(id_);
  }
}

zx_status_t PipeIo::TransferLocked(int32_t cmd, int32_t wake_cmd, zx_signals_t state_clr,
                                   uint32_t write_bytes, uint32_t read_bytes, uint32_t* actual) {
  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd = cmd;
  buffer->status = PIPE_ERROR_INVAL;

  if (write_bytes && read_bytes) {
    buffer->rw_params.ptrs[0] = io_buffer_.phys();
    buffer->rw_params.sizes[0] = write_bytes;
    buffer->rw_params.ptrs[1] = io_buffer_.phys();
    buffer->rw_params.sizes[1] = read_bytes;
    buffer->rw_params.buffers_count = 2;
  } else {
    buffer->rw_params.ptrs[0] = io_buffer_.phys();
    buffer->rw_params.sizes[0] = write_bytes ? write_bytes : read_bytes;
    buffer->rw_params.buffers_count = 1;
  }
  buffer->rw_params.consumed_size = 0;
  buffer->rw_params.read_index = 1;  // Read buffer is always second.
  pipe_->Exec(id_);

  // Positive consumed size always indicate a successful transfer.
  if (buffer->rw_params.consumed_size) {
    *actual = buffer->rw_params.consumed_size;
    return ZX_OK;
  }

  *actual = 0;
  // Early out if error is not because of back-pressure.
  if (buffer->status != PIPE_ERROR_AGAIN) {
    zxlogf(ERROR, "[%s] Pipe::Transfer() transfer failed: %d", __func__, buffer->status);
    return ZX_ERR_INTERNAL;
  }

  buffer->id = id_;
  buffer->cmd = wake_cmd;
  buffer->status = PIPE_ERROR_INVAL;
  pipe_->Exec(id_);
  if (buffer->status) {
    zxlogf(ERROR, "[%s] Pipe::Transfer() failed to request interrupt: %d", __func__,
           buffer->status);
    return ZX_ERR_INTERNAL;
  }

  return ZX_ERR_SHOULD_WAIT;
}

zx_status_t PipeIo::ReadOnceLocked(std::vector<uint8_t>& buf, size_t size) {
  buf.reserve(buf.size() + size);

  uint32_t actual = 0u;
  auto status = TransferLocked(PIPE_CMD_CODE_READ, PIPE_CMD_CODE_WAKE_ON_READ,
                               fuchsia_hardware_goldfish::wire::kSignalReadable, 0u, size, &actual);

  if (status == ZX_OK) {
    std::copy(reinterpret_cast<uint8_t*>(io_buffer_.virt()),
              reinterpret_cast<uint8_t*>(io_buffer_.virt()) + actual, std::back_inserter(buf));
  }
  return status;
}

// Synchronous read.
PipeIo::ReadResult PipeIo::Read(size_t size, bool blocking) {
  std::vector<uint8_t> result;
  result.reserve(size);

  fbl::AutoLock lock(&lock_);

  while (result.size() < size) {
    auto status = ReadOnceLocked(result, size - result.size());

    switch (status) {
      case ZX_OK:
        break;
      case ZX_ERR_SHOULD_WAIT: {
        if (!blocking) {
          return fit::error(status);
        }
        auto wait_status =
            pipe_event_.wait_one(fuchsia_hardware_goldfish::wire::kSignalHangup |
                                     fuchsia_hardware_goldfish::wire::kSignalReadable,
                                 zx::time::infinite(), nullptr);
        if (wait_status != ZX_OK) {
          return fit::error(status);
        }
        break;
      }
      default:
        return fit::error(status);
    }
  }
  result.push_back(0u);
  return fit::ok(std::move(result));
}

PipeIo::ReadResult PipeIo::ReadWithHeader(bool blocking) {
  constexpr size_t kHeaderSize = 4u;

  auto header_result = Read(kHeaderSize, blocking);
  if (!header_result.is_ok()) {
    return header_result;
  }

  uint32_t msg_size;
  sscanf(reinterpret_cast<const char*>(header_result.value().data()), "%04x", &msg_size);
  return Read(msg_size, blocking);
}

zx_status_t PipeIo::Write(const char* payload, bool blocking) {
  size_t len = strlen(payload);
  std::vector<uint8_t> payload_v(payload, payload + len);
  return Write(payload_v, blocking);
}

zx_status_t PipeIo::Write(const std::vector<uint8_t>& payload, bool blocking) {
  if (payload.size() > io_buffer_size_) {
    zxlogf(ERROR, "%s: payload size (%lu) larger than pipe buffer size (%lu)", __func__,
           payload.size(), io_buffer_size_);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::AutoLock lock(&lock_);

  uint32_t actual = 0u;

  uint32_t offset = 0u;
  uint32_t bytes_to_write = payload.size();
  std::copy(payload.begin(), payload.end(), reinterpret_cast<uint8_t*>(io_buffer_.virt()));

  while (bytes_to_write > 0) {
    auto status = TransferLocked(PIPE_CMD_CODE_WRITE, PIPE_CMD_CODE_WAKE_ON_WRITE,
                                 fuchsia_hardware_goldfish::wire::kSignalWritable, offset,
                                 bytes_to_write, &actual);

    switch (status) {
      case ZX_OK:
        offset += actual;
        bytes_to_write -= actual;
        break;
      case ZX_ERR_SHOULD_WAIT: {
        if (!blocking) {
          return status;
        }
        auto wait_status =
            pipe_event_.wait_one(fuchsia_hardware_goldfish::wire::kSignalHangup |
                                     fuchsia_hardware_goldfish::wire::kSignalWritable,
                                 zx::time::infinite(), nullptr);
        if (wait_status != ZX_OK) {
          zxlogf(ERROR, "%s: zx_object_wait_one error (status=%d)", __func__, wait_status);
          return status;
        }
        break;
      }
      default:
        zxlogf(ERROR, "%s: TransferLocked error (status=%d)", __func__, status);
        return status;
    }
  }

  return ZX_OK;
}

zx_status_t PipeIo::WriteWithHeader(const char* payload, bool blocking) {
  size_t len = strlen(payload);
  std::vector<uint8_t> payload_v(payload, payload + len);
  return WriteWithHeader(payload_v, blocking);
}

zx_status_t PipeIo::WriteWithHeader(const std::vector<uint8_t>& payload, bool blocking) {
  constexpr size_t kHeaderSize = 4u;
  if (payload.size() > 0xffffu) {
    zxlogf(ERROR, "%s: payload size (%lu) too large, cannot be expressed in header", __func__,
           payload.size());
    return ZX_ERR_INVALID_ARGS;
  }

  char size[5];
  snprintf(size, sizeof(size), "%04lx", payload.size());

  std::vector<uint8_t> payload_with_header(payload.size() + kHeaderSize);
  memcpy(payload_with_header.data(), size, kHeaderSize);
  memcpy(payload_with_header.data() + kHeaderSize, payload.data(), payload.size());

  return Write(payload_with_header, blocking);
}

}  // namespace goldfish
