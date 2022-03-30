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
#include <fbl/auto_lock.h>

#include "fbl/auto_lock.h"

namespace goldfish::sensor {
namespace testing {

namespace {

constexpr zx_paddr_t kIoBufferPaddr = 0x10000000;
constexpr zx_paddr_t kPinnedVmoPaddr = 0x20000000;
const std::array<zx_paddr_t, 2> kFakeBtiPaddrs = {kIoBufferPaddr, kPinnedVmoPaddr};

}  // namespace

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

void FakePipe::GoldfishPipeDestroy(int32_t id) {
  fbl::AutoLock lock(&lock_);

  pipe_cmd_buffer_.reset();
}

void FakePipe::GoldfishPipeOpen(int32_t id) {
  auto mapping = MapCmdBuffer();
  reinterpret_cast<pipe_cmd_buffer_t*>(mapping.start())->status = 0;

  pipe_opened_ = true;
}

void FakePipe::EnqueueBytesToRead(const std::vector<uint8_t>& bytes) {
  fbl::AutoLock lock(&lock_);
  bytes_to_read_.push(bytes);
}

void FakePipe::GoldfishPipeExec(int32_t id) {
  auto mapping = MapCmdBuffer();
  pipe_cmd_buffer_t* cmd_buffer = reinterpret_cast<pipe_cmd_buffer_t*>(mapping.start());
  cmd_buffer->rw_params.consumed_size = 0;
  cmd_buffer->status = 0;

  if (cmd_buffer->cmd == PIPE_CMD_CODE_WRITE || cmd_buffer->cmd == PIPE_CMD_CODE_CALL) {
    // Store io buffer contents.
    auto io_buffer = MapIoBuffer();

    const size_t write_buffer_begin = 0;
    const size_t write_buffer_end = cmd_buffer->cmd == PIPE_CMD_CODE_CALL
                                        ? cmd_buffer->rw_params.read_index
                                        : cmd_buffer->rw_params.buffers_count;

    for (size_t i = write_buffer_begin; i < write_buffer_end; i++) {
      auto phy_addr = cmd_buffer->rw_params.ptrs[i];
      if (phy_addr >= kIoBufferPaddr && phy_addr < kIoBufferPaddr + io_buffer_size_) {
        auto offset = phy_addr - kIoBufferPaddr;
        auto size = std::min(size_t(cmd_buffer->rw_params.sizes[i]), io_buffer_size_);
        io_buffer_contents_.emplace_back(std::vector<uint8_t>(size, 0));
        memcpy(io_buffer_contents_.back().data(),
               reinterpret_cast<const uint8_t*>(io_buffer.start()) + offset, size);
        if (on_cmd_write_ != nullptr) {
          on_cmd_write_(io_buffer_contents_.back());
        }
        cmd_buffer->rw_params.consumed_size += size;
      } else if (phy_addr >= kPinnedVmoPaddr) {
        fbl::AutoLock lock(&lock_);
        size_t num_vmos = 0u;
        fake_bti_get_pinned_vmos(bti_->get(), nullptr, 0u, &num_vmos);
        std::vector<fake_bti_pinned_vmo_info_t> pinned_vmos(num_vmos);
        fake_bti_get_pinned_vmos(bti_->get(), pinned_vmos.data(), num_vmos, nullptr);

        ZX_DEBUG_ASSERT(num_vmos >= 2 && phy_addr < kPinnedVmoPaddr + pinned_vmos[1].size);
        size_t size = cmd_buffer->rw_params.sizes[i];
        io_buffer_contents_.emplace_back(std::vector<uint8_t>(size, 0));
        zx_vmo_read(pinned_vmos[1].vmo, io_buffer_contents_.back().data(), pinned_vmos[1].offset,
                    size);
        if (on_cmd_write_ != nullptr) {
          on_cmd_write_(io_buffer_contents_.back());
        }
        cmd_buffer->rw_params.consumed_size += size;
      }
    }
  }

  if (cmd_buffer->cmd == PIPE_CMD_CODE_READ || cmd_buffer->cmd == PIPE_CMD_CODE_CALL) {
    auto io_buffer = MapIoBuffer();

    const size_t read_buffer_begin =
        cmd_buffer->cmd == PIPE_CMD_CODE_CALL ? cmd_buffer->rw_params.read_index : 0;
    const size_t read_buffer_end = cmd_buffer->rw_params.buffers_count;

    fbl::AutoLock lock(&lock_);
    if (bytes_to_read_.empty()) {
      cmd_buffer->status = PIPE_ERROR_AGAIN;
      cmd_buffer->rw_params.consumed_size = 0;
    } else {
      cmd_buffer->status = 0;
      auto read_size = bytes_to_read_.front().size();
      auto read_offset = 0u;
      for (size_t i = read_buffer_begin; i < read_buffer_end; i++) {
        auto phy_addr = cmd_buffer->rw_params.ptrs[i];
        if (phy_addr >= kIoBufferPaddr && phy_addr < kIoBufferPaddr + io_buffer_size_) {
          auto offset = phy_addr - kIoBufferPaddr;
          auto size = std::min(size_t(cmd_buffer->rw_params.sizes[i]), read_size - read_offset);
          memcpy(reinterpret_cast<uint8_t*>(io_buffer.start()) + offset,
                 bytes_to_read_.front().data() + read_offset, size);
          cmd_buffer->rw_params.consumed_size += size;
          read_offset += size;
        } else if (phy_addr >= kPinnedVmoPaddr) {
          size_t num_vmos = 0u;
          fake_bti_get_pinned_vmos(bti_->get(), nullptr, 0u, &num_vmos);
          std::vector<fake_bti_pinned_vmo_info_t> pinned_vmos(num_vmos);
          fake_bti_get_pinned_vmos(bti_->get(), pinned_vmos.data(), num_vmos, nullptr);

          ZX_DEBUG_ASSERT(num_vmos >= 2 && phy_addr < kPinnedVmoPaddr + pinned_vmos[1].size);
          size_t size = std::min(size_t(cmd_buffer->rw_params.sizes[i]), read_size - read_offset);
          zx_vmo_write(pinned_vmos[1].vmo, bytes_to_read_.front().data() + read_offset,
                       pinned_vmos[1].offset, size);
          cmd_buffer->rw_params.consumed_size += size;
          read_offset += size;
        }
        if (read_offset == read_size) {
          break;
        }
      }
      bytes_to_read_.pop();
    }
  }
}

zx_status_t FakePipe::GoldfishPipeGetBti(zx::bti* out_bti) {
  fbl::AutoLock lock(&lock_);

  zx_status_t status = fake_bti_create_with_paddrs(kFakeBtiPaddrs.data(), kFakeBtiPaddrs.size(),
                                                   out_bti->reset_and_get_address());
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
  fbl::AutoLock lock(&lock_);

  zx_status_t status;
  if (!pipe_io_buffer_.is_valid()) {
    status = PrepareIoBuffer();
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

fzl::VmoMapper FakePipe::MapCmdBuffer() {
  fbl::AutoLock lock(&lock_);

  fzl::VmoMapper mapping;
  mapping.Map(pipe_cmd_buffer_, 0, sizeof(pipe_cmd_buffer_t), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
  return mapping;
}

fzl::VmoMapper FakePipe::MapIoBuffer() {
  fbl::AutoLock lock(&lock_);

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
