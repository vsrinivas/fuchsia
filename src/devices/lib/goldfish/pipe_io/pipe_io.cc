// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/lib/goldfish/pipe_io/pipe_io.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/result.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <cstdlib>
#include <cstring>
#include <variant>

#include <fbl/auto_lock.h>

#include "src/lib/fxl/strings/string_number_conversions.h"

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
    zxlogf(ERROR, "no pipe protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = pipe_->GetBti(&bti_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "GetBti failed: %d", status);
    return status;
  }

  status = io_buffer_.Init(bti_.get(), PAGE_SIZE, IO_BUFFER_RW | IO_BUFFER_CONTIG);
  io_buffer_size_ = io_buffer_.size();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Init IO buffer failed: %d", status);
    return status;
  }

  ZX_DEBUG_ASSERT(!pipe_event_.is_valid());
  status = zx::event::create(0u, &pipe_event_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_event_create failed: %d", status);
    return status;
  }

  zx::event pipe_event_dup;
  status = pipe_event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &pipe_event_dup);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_handle_duplicate failed: %d", status);
    return status;
  }

  zx::vmo vmo;
  status = pipe_->Create(&id_, &vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Create pipe failed: %d", status);
    return status;
  }
  status = pipe_->SetEvent(id_, std::move(pipe_event_dup));
  if (status != ZX_OK) {
    zxlogf(ERROR, "SetEvent failed: %d", status);
    return status;
  }

  status = cmd_buffer_.InitVmo(bti_.get(), vmo.get(), 0, IO_BUFFER_RW);
  if (status != ZX_OK) {
    zxlogf(ERROR, "InitVmo failed: %d", status);
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
    zxlogf(ERROR, "Open failed: %d", buffer->status);
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

zx::status<uint32_t> PipeIo::TransferLocked(const TransferOp& op) {
  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());

  buffer->rw_params.consumed_size = 0;
  buffer->rw_params.buffers_count = 1;
  buffer->rw_params.ptrs[0] = std::visit(
      [base_addr = io_buffer_.phys()](const auto& data) -> zx_paddr_t {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, TransferOp::IoBuffer>) {
          return base_addr + data.offset;
        } else if constexpr (std::is_same_v<T, TransferOp::PinnedVmo>) {
          return data.paddr;
        }
        ZX_PANIC("Unreachable");
      },
      op.data);
  buffer->rw_params.sizes[0] = op.size;
  buffer->rw_params.read_index = 0;

  return ExecTransferCommandLocked(op.type == TransferOp::Type::kWrite,
                                   op.type == TransferOp::Type::kRead);
}

zx::status<uint32_t> PipeIo::TransferLocked(cpp20::span<const TransferOp> ops) {
  ZX_DEBUG_ASSERT(!ops.empty());
  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());

  bool has_read = false;
  bool has_write = false;
  buffer->rw_params.consumed_size = 0;
  buffer->rw_params.buffers_count = ops.size();
  for (size_t i = 0; i < ops.size(); i++) {
    switch (ops[i].type) {
      case TransferOp::Type::kWrite:
        if (has_read) {
          zxlogf(ERROR, "Read (idx=%lu) must occur after all writes", i);
          return zx::error(ZX_ERR_INVALID_ARGS);
        }
        has_write = true;
        break;
      case TransferOp::Type::kRead:
        if (!has_read) {
          buffer->rw_params.read_index = i;
        }
        has_read = true;
        break;
    }

    buffer->rw_params.ptrs[i] = std::visit(
        [base_addr = io_buffer_.phys()](const auto& data) -> zx_paddr_t {
          using T = std::decay_t<decltype(data)>;
          if constexpr (std::is_same_v<T, TransferOp::IoBuffer>) {
            return base_addr + data.offset;
          } else if constexpr (std::is_same_v<T, TransferOp::PinnedVmo>) {
            return data.paddr;
          }
          ZX_PANIC("Unreachable");
        },
        ops[i].data);

    buffer->rw_params.sizes[i] = ops[i].size;
  }

  return ExecTransferCommandLocked(has_write, has_read);
}

zx::status<uint32_t> PipeIo::ExecTransferCommandLocked(bool has_write, bool has_read) {
  ZX_DEBUG_ASSERT(has_write || has_read);
  auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
  buffer->id = id_;
  buffer->cmd =
      has_read ? (has_write ? PIPE_CMD_CODE_CALL : PIPE_CMD_CODE_READ) : PIPE_CMD_CODE_WRITE;
  buffer->status = PIPE_ERROR_INVAL;
  pipe_->Exec(id_);

  // Positive consumed size always indicate a successful transfer.
  if (buffer->rw_params.consumed_size) {
    return zx::ok(buffer->rw_params.consumed_size);
  }

  // Early out if error is not because of back-pressure.
  if (buffer->status != PIPE_ERROR_AGAIN) {
    zxlogf(ERROR, "Pipe::Transfer() transfer failed: %d", buffer->status);
    return zx::error_status(ZX_ERR_INTERNAL);
  }

  uint32_t clear_events = 0u;
  if (has_read) {
    clear_events |= fuchsia_hardware_goldfish::wire::kSignalReadable;
  }
  if (has_write) {
    clear_events |= fuchsia_hardware_goldfish::wire::kSignalWritable;
  }
  pipe_event_.signal(clear_events, 0u);

  buffer->id = id_;
  buffer->cmd = has_write ? PIPE_CMD_CODE_WAKE_ON_WRITE : PIPE_CMD_CODE_WAKE_ON_READ;
  buffer->status = PIPE_ERROR_INVAL;
  pipe_->Exec(id_);

  if (buffer->status) {
    zxlogf(ERROR, "Pipe::Transfer() failed to request interrupt: %d", buffer->status);
    return zx::error_status(ZX_ERR_INTERNAL);
  }

  return zx::error_status(ZX_ERR_SHOULD_WAIT);
}

PipeIo::ReadResult<char> PipeIo::ReadWithHeader(bool blocking) {
  constexpr size_t kHeaderSize = 4u;

  auto header_result = Read<char>(kHeaderSize, blocking);
  if (!header_result.is_ok()) {
    return header_result;
  }

  uint32_t msg_size = 0u;
  bool convert_result =
      fxl::StringToNumberWithError<uint32_t>(header_result.value(), &msg_size, fxl::Base::k16);
  ZX_DEBUG_ASSERT(convert_result);
  return Read<char>(msg_size, blocking);
}

zx::status<size_t> PipeIo::ReadOnceLocked(void* buf, size_t size) {
  auto status = TransferLocked(TransferOp{
      .type = TransferOp::Type::kRead,
      .data = TransferOp::IoBuffer{.offset = 0u},
      .size = size,
  });

  if (status.is_ok()) {
    memcpy(buf, io_buffer_.virt(), status.value());
  }
  return status;
}

// Synchronous read.
zx_status_t PipeIo::ReadTo(void* dst, size_t size, bool blocking) {
  fbl::AutoLock lock(&lock_);

  size_t bytes_to_read = size;

  while (bytes_to_read > 0) {
    auto status = ReadOnceLocked(dst, bytes_to_read);

    switch (status.status_value()) {
      case ZX_OK:
        dst = reinterpret_cast<uint8_t*>(dst) + status.value();
        bytes_to_read -= status.value();
        break;
      case ZX_ERR_SHOULD_WAIT: {
        if (!blocking) {
          return status.error_value();
        }
        auto wait_status =
            pipe_event_.wait_one(fuchsia_hardware_goldfish::wire::kSignalHangup |
                                     fuchsia_hardware_goldfish::wire::kSignalReadable,
                                 zx::time::infinite(), nullptr);
        if (wait_status != ZX_OK) {
          return status.error_value();
        }
        break;
      }
      default:
        return status.error_value();
    }
  }
  return ZX_OK;
}

zx_status_t PipeIo::CallTo(cpp20::span<const WriteSrc> sources, void* read_dst, size_t read_size,
                           bool blocking) {
  fbl::AutoLock lock(&lock_);

  std::vector<TransferOp> transfer_ops;
  size_t io_buffer_offset = 0u;
  for (const auto& src : sources) {
    if (auto str = std::get_if<WriteSrc::Str>(&src.data); str != nullptr) {
      if (io_buffer_offset + str->length() + 1 > io_buffer_size_) {
        zxlogf(ERROR, "payload size (%lu) exceeded limit (%lu)",
               io_buffer_offset + str->length() + 1, io_buffer_size_);
        return ZX_ERR_INVALID_ARGS;
      }

      char* target_buf = reinterpret_cast<char*>(io_buffer_.virt()) + io_buffer_offset;
      std::copy(str->begin(), str->end(), target_buf);
      target_buf[str->length()] = '\0';

      transfer_ops.push_back(TransferOp{
          .type = TransferOp::Type::kWrite,
          .data = TransferOp::IoBuffer{.offset = io_buffer_offset},
          .size = str->length() + 1,
      });
      io_buffer_offset += str->length() + 1;
    } else if (auto span = std::get_if<WriteSrc::Span>(&src.data); span != nullptr) {
      if (io_buffer_offset + span->size() > io_buffer_size_) {
        zxlogf(ERROR, "payload size (%lu) exceeded limit (%lu)", io_buffer_offset + span->size(),
               io_buffer_size_);
        return ZX_ERR_INVALID_ARGS;
      }

      std::copy(span->begin(), span->end(),
                reinterpret_cast<uint8_t*>(io_buffer_.virt()) + io_buffer_offset);
      transfer_ops.push_back(TransferOp{
          .type = TransferOp::Type::kWrite,
          .data = TransferOp::IoBuffer{.offset = io_buffer_offset},
          .size = span->size(),
      });
      io_buffer_offset += span->size();
    } else if (auto pinned_vmo = std::get_if<WriteSrc::PinnedVmo>(&src.data);
               pinned_vmo != nullptr) {
      ZX_DEBUG_ASSERT(pinned_vmo->vmo && pinned_vmo->vmo->region_count() == 1 &&
                      pinned_vmo->size <= pinned_vmo->vmo->region(0).size);

      transfer_ops.push_back(TransferOp{
          .type = TransferOp::Type::kWrite,
          .data = TransferOp::PinnedVmo{.paddr = pinned_vmo->vmo->region(0).phys_addr +
                                                 pinned_vmo->offset},
          .size = pinned_vmo->size,
      });
    } else {
      ZX_PANIC("Unreachable");
    }
  }

  if (read_size > 0) {
    transfer_ops.push_back(TransferOp{
        .type = TransferOp::Type::kRead,
        .data =
            TransferOp::IoBuffer{
                .offset = 0u,
            },
        .size = read_size,
    });
  }

  auto current_op = transfer_ops.begin();
  while (current_op != transfer_ops.end()) {
    auto status = TransferLocked({current_op, transfer_ops.end()});

    switch (status.status_value()) {
      case ZX_OK: {
        auto actual = status.value();
        while (current_op != transfer_ops.end()) {
          if (current_op->size <= actual) {
            actual -= current_op->size;
            current_op++;
          } else {
            current_op->size -= actual;
            break;
          }
        }
        break;
      }
      case ZX_ERR_SHOULD_WAIT: {
        if (!blocking) {
          return status.status_value();
        }
        bool is_reading = current_op->type == TransferOp::Type::kRead;
        zx_signals_t wait_signals = fuchsia_hardware_goldfish::wire::kSignalHangup |
                                    (is_reading ? fuchsia_hardware_goldfish::wire::kSignalReadable
                                                : fuchsia_hardware_goldfish::wire::kSignalWritable);
        auto wait_status = ZX_OK;
        pipe_event_.wait_one(wait_signals, zx::time::infinite(), nullptr);
        if (wait_status != ZX_OK) {
          zxlogf(ERROR, "zx_object_wait_one error (status=%d)", wait_status);
          return status.status_value();
        }
        break;
      }
      default:
        zxlogf(ERROR, "TransferLocked error (status=%d)", status.status_value());
        return status.status_value();
    }
  }

  memcpy(read_dst, io_buffer_.virt(), read_size);

  return ZX_OK;
}

zx_status_t PipeIo::Write(cpp20::span<const WriteSrc> sources, bool blocking) {
  auto result = Call<char>(sources, 0u, blocking);
  if (result.is_error()) {
    return result.error_value();
  }
  return ZX_OK;
}

zx_status_t PipeIo::Write(const char* payload, bool blocking) {
  WriteSrc sources[] = {{.data = payload}};
  return Write(sources, blocking);
}

zx_status_t PipeIo::Write(const std::vector<uint8_t>& payload, bool blocking) {
  WriteSrc sources[] = {{.data = payload}};
  return Write(sources, blocking);
}

zx_status_t PipeIo::WriteWithHeader(const char* payload, bool blocking) {
  size_t len = strlen(payload);
  std::vector<uint8_t> payload_v(payload, payload + len);
  return WriteWithHeader(payload_v, blocking);
}

zx_status_t PipeIo::WriteWithHeader(const std::vector<uint8_t>& payload, bool blocking) {
  constexpr size_t kHeaderSize = 4u;
  if (payload.size() > 0xffffu) {
    zxlogf(ERROR, "payload size (%lu) too large, cannot be expressed in header", payload.size());
    return ZX_ERR_INVALID_ARGS;
  }

  char size[5];
  snprintf(size, sizeof(size), "%04lx", payload.size());

  std::vector<uint8_t> payload_with_header(payload.size() + kHeaderSize);
  memcpy(payload_with_header.data(), size, kHeaderSize);
  memcpy(payload_with_header.data() + kHeaderSize, payload.data(), payload.size());

  return Write(payload_with_header, blocking);
}

fzl::PinnedVmo PipeIo::PinVmo(zx::vmo& vmo, uint32_t options) {
  fbl::AutoLock lock(&lock_);
  fzl::PinnedVmo result;
  auto status = result.Pin(vmo, bti_, options);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  return result;
}

fzl::PinnedVmo PipeIo::PinVmo(zx::vmo& vmo, uint32_t options, size_t offset, size_t size) {
  fbl::AutoLock lock(&lock_);
  fzl::PinnedVmo result;
  auto status = result.PinRange(offset, size, vmo, bti_, options);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  return result;
}

}  // namespace goldfish
