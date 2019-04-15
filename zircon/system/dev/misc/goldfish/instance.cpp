// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "instance.h"

#include <ddk/debug.h>
#include <ddk/trace/event.h>

#include <fuchsia/hardware/goldfish/pipe/c/fidl.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/bti.h>

#include <algorithm>

namespace goldfish {
namespace {

const char* kTag = "goldfish-pipe";

constexpr size_t DEFAULT_BUFFER_SIZE = 8192;

constexpr zx_signals_t SIGNALS =
    fuchsia_hardware_goldfish_pipe_SIGNAL_READABLE |
    fuchsia_hardware_goldfish_pipe_SIGNAL_WRITABLE;

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

    status = SetBufferSize(DEFAULT_BUFFER_SIZE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to set initial buffer size\n", kTag);
        return status;
    }

    status = zx::event::create(0, &buffer_.event);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to set initial buffer size\n", kTag);
        return status;
    }
    buffer_.event.signal(0, SIGNALS);

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

zx_status_t Instance::FidlSetBufferSize(uint64_t size, fidl_txn_t* txn) {
    TRACE_DURATION("gfx", "Instance::FidlSetBufferSize", "size", size);

    zx_status_t status = SetBufferSize(size);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to set buffer size: %lu\n", kTag, size);
        return status;
    }

    return fuchsia_hardware_goldfish_pipe_DeviceSetBufferSize_reply(txn,
                                                                    status);
}

zx_status_t Instance::FidlSetEvent(zx_handle_t event_handle) {
    TRACE_DURATION("gfx", "Instance::FidlSetEvent");

    zx::event event(event_handle);
    if (!event.is_valid()) {
        zxlogf(ERROR, "%s: invalid event\n", kTag);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_handle_t observed = 0;
    zx_status_t status =
        zx_object_wait_one(buffer_.event.get(), SIGNALS, 0, &observed);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: failed to transfer observed signals: %d\n", kTag,
               status);
        return status;
    }

    buffer_.event = std::move(event);
    buffer_.event.signal(SIGNALS, observed);
    return ZX_OK;
}

zx_status_t Instance::FidlGetBuffer(fidl_txn_t* txn) {
    TRACE_DURATION("gfx", "Instance::FidlGetBuffer");

    zx::vmo vmo;
    zx_status_t status = buffer_.vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_vmo_duplicate failed: %d\n", kTag, status);
        return status;
    }

    return fuchsia_hardware_goldfish_pipe_DeviceGetBuffer_reply(txn, ZX_OK,
                                                                vmo.release());
}

zx_status_t Instance::FidlRead(size_t count, zx_off_t offset, fidl_txn_t* txn) {
    TRACE_DURATION("gfx", "Instance::FidlRead", "count", count);

    if ((offset + count) > buffer_.size)
        return ZX_ERR_INVALID_ARGS;

    size_t actual;
    zx_status_t status = Read(buffer_.phys + offset, count, &actual);
    return fuchsia_hardware_goldfish_pipe_DeviceRead_reply(txn, status, actual);
}

zx_status_t Instance::FidlWrite(size_t count, zx_off_t offset,
                                fidl_txn_t* txn) {
    TRACE_DURATION("gfx", "Instance::FidlWrite", "count", count);

    if ((offset + count) > buffer_.size)
        return ZX_ERR_INVALID_ARGS;

    size_t actual;
    zx_status_t status = Write(buffer_.phys + offset, count, &actual);
    return fuchsia_hardware_goldfish_pipe_DeviceWrite_reply(txn, status,
                                                            actual);
}

zx_status_t Instance::DdkRead(void* buf, size_t buf_len, zx_off_t off,
                              size_t* actual) {
    TRACE_DURATION("gfx", "Instance::DdkRead", "buf_len", buf_len);

    size_t count = std::min(buf_len, buffer_.size);
    zx_status_t status = Read(buffer_.phys, count, actual);
    if (status != ZX_OK) {
        return status;
    }
    status = buffer_.vmo.read(buf, 0, *actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_vmo_read failed %d size: %zu\n", kTag, status,
               *actual);
        return status;
    }
    return ZX_OK;
}

zx_status_t Instance::DdkWrite(const void* buf, size_t buf_len, zx_off_t off,
                               size_t* actual) {
    TRACE_DURATION("gfx", "Instance::DdkWrite", "buf_len", buf_len);

    size_t count = std::min(buf_len, buffer_.size);
    zx_status_t status = buffer_.vmo.write(buf, 0, count);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_vmo_write failed %d size: %zu\n", kTag, status,
               count);
        return status;
    }
    return Write(buffer_.phys, count, actual);
}

zx_status_t Instance::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    using Binder = fidl::Binder<Instance>;

    static const fuchsia_hardware_goldfish_pipe_Device_ops_t kOps = {
        .SetBufferSize = Binder::BindMember<&Instance::FidlSetBufferSize>,
        .SetEvent = Binder::BindMember<&Instance::FidlSetEvent>,
        .GetBuffer = Binder::BindMember<&Instance::FidlGetBuffer>,
        .Read = Binder::BindMember<&Instance::FidlRead>,
        .Write = Binder::BindMember<&Instance::FidlWrite>,
    };

    return fuchsia_hardware_goldfish_pipe_Device_dispatch(this, txn, msg,
                                                          &kOps);
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

    zx_signals_t dev_state_set = 0;
    zx_signals_t state_set = 0;
    if (flags & PIPE_WAKE_FLAG_CLOSED) {
        dev_state_set = DEV_STATE_HANGUP;
        state_set = fuchsia_hardware_goldfish_pipe_SIGNAL_HANGUP;
    }
    if (flags & PIPE_WAKE_FLAG_READ) {
        dev_state_set = DEV_STATE_READABLE;
        state_set = fuchsia_hardware_goldfish_pipe_SIGNAL_READABLE;
    }
    if (flags & PIPE_WAKE_FLAG_WRITE) {
        dev_state_set = DEV_STATE_WRITABLE;
        state_set = fuchsia_hardware_goldfish_pipe_SIGNAL_WRITABLE;
    }

    auto instance = static_cast<Instance*>(ctx);
    instance->SetState(dev_state_set);
    instance->buffer_.event.signal(0, state_set);
}

zx_status_t Instance::SetBufferSize(size_t size) {
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create_contiguous(bti_, size, 0, &vmo);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_vmo_create_contiguous failed %d size: %zu\n",
               kTag, status, size);
        return status;
    }

    zx_paddr_t phys;
    zx::pmt pmt;
    status = bti_.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, vmo, 0, size, &phys,
                      1, &pmt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: zx_bti_pin failed %d size: %zu\n", kTag, status,
               size);
        return status;
    }

    buffer_.vmo = std::move(vmo);
    buffer_.pmt = std::move(pmt);
    buffer_.size = size;
    buffer_.phys = phys;
    return ZX_OK;
}

zx_status_t Instance::Read(zx_paddr_t paddr, size_t count, size_t* actual) {
    return Transfer(PIPE_CMD_CODE_READ, PIPE_CMD_CODE_WAKE_ON_READ,
                    fuchsia_hardware_goldfish_pipe_SIGNAL_READABLE,
                    DEV_STATE_READABLE, paddr, count, actual);
}

zx_status_t Instance::Write(zx_paddr_t paddr, size_t count, size_t* actual) {
    return Transfer(PIPE_CMD_CODE_WRITE, PIPE_CMD_CODE_WAKE_ON_WRITE,
                    fuchsia_hardware_goldfish_pipe_SIGNAL_WRITABLE,
                    DEV_STATE_WRITABLE, paddr, count, actual);
}

zx_status_t Instance::Transfer(int32_t cmd, int32_t wake_cmd,
                               zx_signals_t state_clr,
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
    buffer_.event.signal(state_clr, 0);

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
