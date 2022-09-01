// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/testing/goldfish/fake_pipe/fake_pipe.h"

#include <fidl/fuchsia.hardware.goldfish.pipe/cpp/wire.h>
#include <lib/fake-bti/bti.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/bti.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include <queue>
#include <variant>

#include <ddktl/device.h>
#include <fbl/auto_lock.h>

#include "src/devices/lib/goldfish/pipe_headers/include/base.h"

namespace goldfish::sensor {
namespace testing {

namespace {

constexpr zx_paddr_t kIoBufferPaddr = 0x10000000;
constexpr zx_paddr_t kPinnedVmoPaddr = 0x20000000;
const std::array<zx_paddr_t, 2> kFakeBtiPaddrs = {kIoBufferPaddr, kPinnedVmoPaddr};

}  // namespace

void FakePipe::Create(CreateCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(PAGE_SIZE, 0u, &vmo);
  vmo.set_cache_policy(ZX_CACHE_POLICY_UNCACHED);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &pipe_cmd_buffer_);
  pipe_cmd_buffer_.set_cache_policy(ZX_CACHE_POLICY_UNCACHED);
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }

  pipe_created_ = true;
  completer.ReplySuccess(kPipeId, std::move(vmo));
}

void FakePipe::SetEvent(SetEventRequestView request, SetEventCompleter::Sync& completer) {
  if (request->id != kPipeId) {
    completer.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (!request->pipe_event.is_valid()) {
    completer.Close(ZX_ERR_BAD_HANDLE);
    return;
  }
  pipe_event_ = std::move(request->pipe_event);
  completer.ReplySuccess();
}

void FakePipe::Destroy(DestroyRequestView request, DestroyCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);

  pipe_cmd_buffer_.reset();
  completer.Reply();
}

void FakePipe::Open(OpenRequestView request, OpenCompleter::Sync& completer) {
  auto mapping = MapCmdBuffer();
  reinterpret_cast<PipeCmdBuffer*>(mapping.start())->status = 0;

  pipe_opened_ = true;
  completer.Reply();
}

void FakePipe::EnqueueBytesToRead(const std::vector<uint8_t>& bytes) {
  fbl::AutoLock lock(&lock_);
  bytes_to_read_.push(bytes);
}

void FakePipe::Exec(ExecRequestView request, ExecCompleter::Sync& completer) {
  auto mapping = MapCmdBuffer();
  PipeCmdBuffer* cmd_buffer = reinterpret_cast<PipeCmdBuffer*>(mapping.start());
  cmd_buffer->rw_params.consumed_size = 0;
  cmd_buffer->status = 0;

  if (cmd_buffer->cmd ==
          static_cast<int32_t>(fuchsia_hardware_goldfish_pipe::PipeCmdCode::kWrite) ||
      cmd_buffer->cmd == static_cast<int32_t>(fuchsia_hardware_goldfish_pipe::PipeCmdCode::kCall)) {
    // Store io buffer contents.
    auto io_buffer = MapIoBuffer();

    const size_t write_buffer_begin = 0;
    const size_t write_buffer_end =
        cmd_buffer->cmd == static_cast<int32_t>(fuchsia_hardware_goldfish_pipe::PipeCmdCode::kCall)
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

  if (cmd_buffer->cmd == static_cast<int32_t>(fuchsia_hardware_goldfish_pipe::PipeCmdCode::kRead) ||
      cmd_buffer->cmd == static_cast<int32_t>(fuchsia_hardware_goldfish_pipe::PipeCmdCode::kCall)) {
    auto io_buffer = MapIoBuffer();

    const size_t read_buffer_begin =
        cmd_buffer->cmd == static_cast<int32_t>(fuchsia_hardware_goldfish_pipe::PipeCmdCode::kCall)
            ? cmd_buffer->rw_params.read_index
            : 0;
    const size_t read_buffer_end = cmd_buffer->rw_params.buffers_count;

    fbl::AutoLock lock(&lock_);
    if (bytes_to_read_.empty()) {
      cmd_buffer->status = static_cast<int32_t>(fuchsia_hardware_goldfish_pipe::PipeError::kAgain);
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

  completer.Reply();
}

void FakePipe::GetBti(GetBtiCompleter::Sync& completer) {
  fbl::AutoLock lock(&lock_);

  zx::bti bti;
  zx_status_t status = fake_bti_create_with_paddrs(kFakeBtiPaddrs.data(), kFakeBtiPaddrs.size(),
                                                   bti.reset_and_get_address());
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  bti_ = bti.borrow();
  completer.ReplySuccess(std::move(bti));
}

void FakePipe::ConnectSysmem(ConnectSysmemRequestView request,
                             ConnectSysmemCompleter::Sync& completer) {
  completer.ReplySuccess();
}

void FakePipe::RegisterSysmemHeap(RegisterSysmemHeapRequestView request,
                                  RegisterSysmemHeapCompleter::Sync& completer) {
  completer.ReplySuccess();
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
  mapping.Map(pipe_cmd_buffer_, 0, sizeof(PipeCmdBuffer), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE);
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
