// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/testing/goldfish/fake_pipe/fake_pipe.h"

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/fake-bti/bti.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <queue>
#include <variant>

#include <ddktl/device.h>

namespace goldfish::sensor {
namespace testing {

FakePipe::FakePipe() : proto_({&goldfish_pipe_protocol_ops_, this}) {}

const goldfish_pipe_protocol_t* FakePipe::proto() const { return &proto_; }

zx_status_t FakePipe::GoldfishPipeCreate(int32_t* out_id, zx::vmo* out_vmo) {
  *out_id = kPipeId;
  zx_status_t status = zx::vmo::create(PAGE_SIZE, 0u, out_vmo);
  if (status != ZX_OK) {
    return status;
  }
  status = out_vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &pipe_cmd_buffer_);
  if (status != ZX_OK) {
    return status;
  }

  pipe_created_ = true;
  return ZX_OK;
}

zx_status_t FakePipe::GoldfishPipeSetEvent(int32_t id, zx::event pipe_event) {
  if (id != kPipeId) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!pipe_event.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }
  pipe_event_ = std::move(pipe_event);
  return ZX_OK;
}

void FakePipe::GoldfishPipeDestroy(int32_t id) { pipe_cmd_buffer_.reset(); }

void FakePipe::GoldfishPipeOpen(int32_t id) {
  auto mapping = MapCmdBuffer();
  reinterpret_cast<pipe_cmd_buffer_t*>(mapping.start())->status = 0;

  pipe_opened_ = true;
}

void FakePipe::EnqueueBytesToRead(const std::vector<uint8_t>& bytes) { bytes_to_read_.push(bytes); }

void FakePipe::GoldfishPipeExec(int32_t id) {
  auto mapping = MapCmdBuffer();
  pipe_cmd_buffer_t* cmd_buffer = reinterpret_cast<pipe_cmd_buffer_t*>(mapping.start());
  cmd_buffer->rw_params.consumed_size = cmd_buffer->rw_params.sizes[0];
  cmd_buffer->status = 0;

  if (cmd_buffer->cmd == PIPE_CMD_CODE_WRITE) {
    // Store io buffer contents.
    auto io_buffer = MapIoBuffer();
    auto size = std::min(size_t(cmd_buffer->rw_params.consumed_size), io_buffer_size_);
    io_buffer_contents_.emplace_back(std::vector<uint8_t>(size, 0));
    memcpy(io_buffer_contents_.back().data(), io_buffer.start(), size);
    if (on_cmd_write_ != nullptr) {
      on_cmd_write_(io_buffer_contents_.back());
    }
  }

  if (cmd_buffer->cmd == PIPE_CMD_CODE_READ) {
    auto io_buffer = MapIoBuffer();
    if (bytes_to_read_.empty()) {
      cmd_buffer->status = PIPE_ERROR_AGAIN;
      cmd_buffer->rw_params.consumed_size = 0;
    } else {
      cmd_buffer->status = 0;
      auto read_size = std::min(io_buffer.size(), bytes_to_read_.front().size());
      cmd_buffer->rw_params.consumed_size = read_size;
      memcpy(io_buffer.start(), bytes_to_read_.front().data(), read_size);
      bytes_to_read_.pop();
    }
  }
}

zx_status_t FakePipe::GoldfishPipeGetBti(zx::bti* out_bti) {
  zx_status_t status = fake_bti_create(out_bti->reset_and_get_address());
  if (status == ZX_OK) {
    bti_ = out_bti->borrow();
  }
  return status;
}

zx_status_t FakePipe::GoldfishPipeConnectSysmem(zx::channel connection) { return ZX_OK; }

zx_status_t FakePipe::GoldfishPipeRegisterSysmemHeap(uint64_t heap, zx::channel connection) {
  return ZX_OK;
}

zx_status_t FakePipe::SetUpPipeDevice() {
  zx_status_t status;
  if (!pipe_io_buffer_.is_valid()) {
    status = PrepareIoBuffer();
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

fzl::VmoMapper FakePipe::MapCmdBuffer() const {
  fzl::VmoMapper mapping;
  mapping.Map(pipe_cmd_buffer_, 0, sizeof(pipe_cmd_buffer_t), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  return mapping;
}

fzl::VmoMapper FakePipe::MapIoBuffer() {
  if (!pipe_io_buffer_.is_valid()) {
    PrepareIoBuffer();
  }
  fzl::VmoMapper mapping;
  mapping.Map(pipe_io_buffer_, 0, io_buffer_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  return mapping;
}

void FakePipe::SetOnCmdWriteCallback(fit::function<void(const std::vector<uint8_t>&)> fn) {
  on_cmd_write_ = std::move(fn);
}

bool FakePipe::IsPipeReady() const { return pipe_created_ && pipe_opened_; }

zx::event& FakePipe::pipe_event() { return pipe_event_; }

const std::vector<std::vector<uint8_t>>& FakePipe::io_buffer_contents() const {
  return io_buffer_contents_;
}

zx_status_t FakePipe::PrepareIoBuffer() {
  uint64_t num_pinned_vmos = 0u;
  std::vector<fake_bti_pinned_vmo_info_t> pinned_vmos;
  zx_status_t status = fake_bti_get_pinned_vmos(bti_->get(), nullptr, 0, &num_pinned_vmos);
  if (status != ZX_OK) {
    return status;
  }
  if (num_pinned_vmos == 0u) {
    return ZX_ERR_NOT_FOUND;
  }

  pinned_vmos.resize(num_pinned_vmos);
  status = fake_bti_get_pinned_vmos(bti_->get(), pinned_vmos.data(), num_pinned_vmos, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  pipe_io_buffer_ = zx::vmo(pinned_vmos.back().vmo);
  pinned_vmos.pop_back();
  // close all the unused handles
  for (auto vmo_info : pinned_vmos) {
    zx_handle_close(vmo_info.vmo);
  }

  status = pipe_io_buffer_.get_size(&io_buffer_size_);
  return status;
}

}  // namespace testing
}  // namespace goldfish::sensor
