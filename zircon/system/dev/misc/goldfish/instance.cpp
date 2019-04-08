// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "instance.h"

#include <ddk/debug.h>
#include <ddk/trace/event.h>

#include <algorithm>

namespace goldfish {
namespace {

const char* kTag = "goldfish-pipe";

constexpr size_t RW_BUFFER_SIZE = 8192;

} // namespace

Instance::Instance(zx_device_t* parent) : InstanceType(parent), pipe_(parent) {}

Instance::~Instance() {
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

zx_status_t Instance::Bind() {
    TRACE_DURATION("gfx", "Instance::Bind");

    if (!pipe_.is_valid()) {
        zxlogf(ERROR, "%s: no pipe protocol\n", kTag);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = pipe_.GetBti(&bti_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBti failed: %d\n", kTag, status);
        return status;
    }

    status = io_buffer_.Init(bti_.get(), RW_BUFFER_SIZE,
                             IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: io_buffer_init failed: %d\n", kTag, status);
        return status;
    }

    zx::vmo vmo;
    goldfish_pipe_signal_value_t signal_cb = {Instance::OnSignal, this};
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

    auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
    buffer->id = id_;
    buffer->cmd = PIPE_CMD_CODE_OPEN;
    buffer->status = PIPE_ERROR_INVAL;

    pipe_.Open(id_);
    if (buffer->status) {
        zxlogf(ERROR, "%s: Open failed: %d\n", kTag, buffer->status);
        cmd_buffer_.release();
        return ZX_ERR_INTERNAL;
    }

    return DdkAdd("pipe", DEVICE_ADD_INSTANCE);
}

zx_status_t Instance::DdkRead(void* buf, size_t buf_len, zx_off_t off,
                              size_t* actual) {
    TRACE_DURATION("gfx", "Instance::DdkRead", "buf_len", buf_len);

    size_t count = std::min(buf_len, RW_BUFFER_SIZE);
    zx_status_t status =
        Transfer(PIPE_CMD_CODE_READ, PIPE_CMD_CODE_WAKE_ON_READ,
                 DEV_STATE_READABLE, io_buffer_.phys(), count, actual);
    memcpy(buf, io_buffer_.virt(), *actual);
    return status;
}

zx_status_t Instance::DdkWrite(const void* buf, size_t buf_len, zx_off_t off,
                               size_t* actual) {
    TRACE_DURATION("gfx", "Instance::DdkWrite", "buf_len", buf_len);

    size_t count = std::min(buf_len, RW_BUFFER_SIZE);
    memcpy(io_buffer_.virt(), buf, count);
    return Transfer(PIPE_CMD_CODE_WRITE, PIPE_CMD_CODE_WAKE_ON_WRITE,
                    DEV_STATE_WRITABLE, io_buffer_.phys(), count, actual);
}

zx_status_t Instance::DdkClose(uint32_t flags) {
    return ZX_OK;
}

void Instance::DdkRelease() {
    delete this;
}

// static
void Instance::OnSignal(void* ctx, int32_t flags) {
    TRACE_DURATION("gfx", "Instance::OnSignal", "flags", flags);

    auto instance = static_cast<Instance*>(ctx);
    if (flags & PIPE_WAKE_FLAG_CLOSED) {
        instance->SetState(DEV_STATE_HANGUP);
    }
    if (flags & PIPE_WAKE_FLAG_READ) {
        instance->SetState(DEV_STATE_READABLE);
    }
    if (flags & PIPE_WAKE_FLAG_WRITE) {
        instance->SetState(DEV_STATE_WRITABLE);
    }
}

zx_status_t Instance::Transfer(int32_t cmd, int32_t wake_cmd,
                               zx_signals_t dev_state_clr, zx_paddr_t paddr,
                               size_t count, size_t* actual) {
    TRACE_DURATION("gfx", "Instance::Transfer", "count", count);

    auto buffer = static_cast<pipe_cmd_buffer_t*>(cmd_buffer_.virt());
    buffer->id = id_;
    buffer->cmd = cmd;
    buffer->status = PIPE_ERROR_INVAL;
    buffer->rw_params.ptrs[0] = paddr;
    buffer->rw_params.sizes[0] = static_cast<uint32_t>(count);
    buffer->rw_params.buffers_count = 1;
    buffer->rw_params.consumed_size = 0;
    pipe_.Exec(id_);

    // Positive consumed size always indicate a successful transfer.
    if (buffer->rw_params.consumed_size) {
        *actual = buffer->rw_params.consumed_size;
        return ZX_OK;
    }

    *actual = 0;
    // Early out if error is not because of back-pressure.
    if (buffer->status != PIPE_ERROR_AGAIN) {
        zxlogf(ERROR, "%s: transfer failed: %d\n", kTag, buffer->status);
        return ZX_ERR_INTERNAL;
    }

    // PIPE_ERROR_AGAIN means that we need to wait until pipe is
    // readable/writable before we can perform another transfer command.
    // Remove device state and request an interrupt that will indicate
    // that the pipe is again readable/writable.
    ClearState(dev_state_clr);

    buffer->id = id_;
    buffer->cmd = wake_cmd;
    buffer->status = PIPE_ERROR_INVAL;
    pipe_.Exec(id_);
    if (buffer->status) {
        zxlogf(ERROR, "%s: failed to request interrupt: %d\n", kTag,
               buffer->status);
        return ZX_ERR_INTERNAL;
    }

    return ZX_ERR_SHOULD_WAIT;
}

} // namespace goldfish
