// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "serial.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/unique_ptr.h>
#include <lib/fidl-utils/bind.h>
#include <lib/zx/handle.h>
#include <lib/zx/time.h>
#include <zircon/threads.h>

namespace serial {

enum {
    WAIT_ITEM_SOCKET,
    WAIT_ITEM_EVENT,
};

namespace {

constexpr serial_notify_t kNoCallback = {nullptr, nullptr};

constexpr size_t kUartBufferSize = 1024;

constexpr zx_signals_t kEventReadableSignal = ZX_USER_SIGNAL_0;
constexpr zx_signals_t kEventWritableSignal = ZX_USER_SIGNAL_1;
constexpr zx_signals_t kEventCancelSignal = ZX_USER_SIGNAL_2;

} // namespace

// This thread handles data transfer in both directions
zx_status_t SerialDevice::WorkerThread() {
    uint8_t in_buffer[kUartBufferSize];
    uint8_t out_buffer[kUartBufferSize];
    size_t in_buffer_offset = 0;  // offset of first byte in in_buffer (if any)
    size_t out_buffer_offset = 0; // offset of first byte in out_buffer (if any)
    size_t in_buffer_count = 0;   // number of bytes in in_buffer
    size_t out_buffer_count = 0;  // number of bytes in out_buffer
    zx_wait_item_t items[2];

    items[WAIT_ITEM_SOCKET].handle = socket_.get();
    items[WAIT_ITEM_EVENT].handle = event_.get();
    bool peer_closed = false;

    // loop until client socket is closed and we have no more data to write
    while (!peer_closed || out_buffer_count > 0) {
        // attempt pending socket write
        if (in_buffer_count > 0) {
            size_t actual;
            zx_status_t status = socket_.write(0, in_buffer + in_buffer_offset,
                                               in_buffer_count, &actual);
            if (status == ZX_OK) {
                in_buffer_count -= actual;
                if (in_buffer_count > 0) {
                    in_buffer_offset += actual;
                } else {
                    in_buffer_offset = 0;
                }
            } else if (status != ZX_ERR_SHOULD_WAIT && status != ZX_ERR_PEER_CLOSED) {
                zxlogf(ERROR, "platform_serial_thread: zx::socket::write returned %d\n", status);
                break;
            }
        }

        // attempt pending serial write
        if (out_buffer_count > 0) {
            size_t actual;
            zx_status_t status = serial_.Write(out_buffer + out_buffer_offset,
                                               out_buffer_count, &actual);
            if (status == ZX_OK) {
                out_buffer_count -= actual;
                if (out_buffer_count > 0) {
                    out_buffer_offset += actual;
                } else {
                    // out_buffer empty now, reset to beginning
                    out_buffer_offset = 0;
                }
            } else if (status != ZX_ERR_SHOULD_WAIT && status != ZX_ERR_PEER_CLOSED) {
                zxlogf(ERROR, "platform_serial_thread: serial_impl_write returned %d\n", status);
                break;
            }
        }

        // wait for serial or socket to be readable
        items[WAIT_ITEM_SOCKET].waitfor = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED;
        items[WAIT_ITEM_EVENT].waitfor = kEventReadableSignal | kEventCancelSignal;
        // also wait for writability if we have pending data to write
        if (in_buffer_count > 0) {
            items[WAIT_ITEM_SOCKET].waitfor |= ZX_SOCKET_WRITABLE;
        }
        if (out_buffer_count > 0) {
            items[WAIT_ITEM_EVENT].waitfor |= kEventWritableSignal;
        }

        zx_status_t status = zx::handle::wait_many(items, countof(items), zx::time::infinite());
        if (status != ZX_OK) {
            zxlogf(ERROR, "platform_serial_thread: zx_object_wait_many returned %d\n", status);
            break;
        }

        if (items[WAIT_ITEM_EVENT].pending & kEventReadableSignal) {
            size_t length;
            status = serial_.Read(in_buffer + in_buffer_count,
                                  sizeof(in_buffer) - in_buffer_count, &length);

            if (status != ZX_OK) {
                zxlogf(ERROR, "platform_serial_thread: serial_impl_read returned %d\n", status);
                break;
            }
            in_buffer_count += length;
        }

        if (items[WAIT_ITEM_SOCKET].pending & ZX_SOCKET_READABLE) {
            size_t length;
            status = socket_.read(0, out_buffer + out_buffer_count,
                                  sizeof(out_buffer) - out_buffer_count, &length);
            if (status != ZX_OK) {
                zxlogf(ERROR, "serial_out_thread: zx::socket::read returned %d\n", status);
                break;
            }
            out_buffer_count += length;
        }
        if (items[WAIT_ITEM_SOCKET].pending & ZX_SOCKET_PEER_CLOSED) {
            peer_closed = true;
        }
    }

    serial_.Enable(false);
    serial_.SetNotifyCallback(&kNoCallback);

    event_.reset();
    socket_.reset();

    fbl::AutoLock al(&lock_);
    open_ = false;

    return 0;
}

void SerialDevice::StateCallback(serial_state_t state) {
    // update our event handle signals with latest state from the serial driver
    zx_signals_t event_set = 0;
    zx_signals_t event_clear = 0;
    zx_signals_t device_set = 0;
    zx_signals_t device_clear = 0;

    if (state & SERIAL_STATE_READABLE) {
        event_set |= kEventReadableSignal;
        device_set |= DEV_STATE_READABLE;
    } else {
        event_clear |= kEventReadableSignal;
        device_clear |= DEV_STATE_READABLE;
    }
    if (state & SERIAL_STATE_WRITABLE) {
        event_set |= kEventWritableSignal;
        device_set |= DEV_STATE_WRITABLE;
    } else {
        event_clear |= kEventWritableSignal;
        device_clear |= DEV_STATE_WRITABLE;
    }

    if (socket_ != ZX_HANDLE_INVALID) {
        // another driver bound to us
        event_.signal(event_clear, event_set);
    } else {
        // someone opened us via /dev file system
        ClearAndSetState(device_clear, device_set);
    }
}

zx_status_t SerialDevice::SerialGetInfo(serial_port_info_t* info) {
    return serial_.GetInfo(info);
}

zx_status_t SerialDevice::SerialConfig(uint32_t baud_rate, uint32_t flags) {
    return serial_.Config(baud_rate, flags);
}

zx_status_t SerialDevice::SerialOpenSocket(zx::socket* out_handle) {
    fbl::AutoLock al(&lock_);
    if (open_) {
        return ZX_ERR_ALREADY_BOUND;
    }

    zx::socket socket(ZX_HANDLE_INVALID);
    zx_status_t status = zx::socket::create(ZX_SOCKET_STREAM, &socket_, &socket);
    if (status != ZX_OK) {
        return status;
    }

    status = zx::event::create(0, &event_);
    if (status != ZX_OK) {
        socket.reset();
        return status;
    }

    const serial_notify_t callback = {
        [](void* arg, serial_state_t state) {
            static_cast<SerialDevice*>(arg)->StateCallback(state);
        },
        this};
    serial_.SetNotifyCallback(&callback);

    status = serial_.Enable(true);
    if (status != ZX_OK) {
        socket.reset();
        return status;
    }

    int thrd_rc = thrd_create_with_name(
        &thread_,
        [](void* arg) { return static_cast<SerialDevice*>(arg)->WorkerThread(); },
        this, "platform_serial_thread");
    if (thrd_rc != thrd_success) {
        socket.reset();
        return thrd_status_to_zx_status(thrd_rc);
    }

    *out_handle = std::move(socket);
    open_ = true;
    return ZX_OK;
}

zx_status_t SerialDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    fbl::AutoLock al(&lock_);

    if (open_) {
        return ZX_ERR_ALREADY_BOUND;
    }

    const serial_notify_t callback = {
        [](void* arg, serial_state_t state) {
            static_cast<SerialDevice*>(arg)->StateCallback(state);
        },
        this};
    serial_.SetNotifyCallback(&callback);

    zx_status_t status = serial_.Enable(true);
    if (status == ZX_OK) {
        open_ = true;
    }

    return status;
}

zx_status_t SerialDevice::DdkClose(uint32_t flags) {
    fbl::AutoLock al(&lock_);

    if (open_) {
        serial_.SetNotifyCallback(&kNoCallback);
        serial_.Enable(false);
        open_ = false;
        return ZX_OK;
    } else {
        zxlogf(ERROR, "SerialDevice::DdkClose called when not open\n");
        return ZX_ERR_BAD_STATE;
    }
}

zx_status_t SerialDevice::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
    fbl::AutoLock al(&lock_);

    if (!open_) {
        return ZX_ERR_BAD_STATE;
    }

    return serial_.Read(buf, count, actual);
}

zx_status_t SerialDevice::DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    fbl::AutoLock al(&lock_);

    if (!open_) {
        return ZX_ERR_BAD_STATE;
    }

    return serial_.Write(buf, count, actual);
}

zx_status_t SerialDevice::FidlSerialGetClass(fidl_txn_t* txn) {
    return fuchsia_hardware_serial_DeviceGetClass_reply(txn, static_cast<uint8_t>(serial_class_));
}

zx_status_t SerialDevice::FidlSerialSetConfig(const fuchsia_hardware_serial_Config* config,
                                              fidl_txn_t* txn) {
    uint32_t flags = 0;
    switch (config->character_width) {
    case fuchsia_hardware_serial_CharacterWidth_BITS_5: flags |= SERIAL_DATA_BITS_5; break;
    case fuchsia_hardware_serial_CharacterWidth_BITS_6: flags |= SERIAL_DATA_BITS_6; break;
    case fuchsia_hardware_serial_CharacterWidth_BITS_7: flags |= SERIAL_DATA_BITS_7; break;
    case fuchsia_hardware_serial_CharacterWidth_BITS_8: flags |= SERIAL_DATA_BITS_8; break;
    }

    switch (config->stop_width) {
    case fuchsia_hardware_serial_StopWidth_BITS_1: flags |= SERIAL_STOP_BITS_1; break;
    case fuchsia_hardware_serial_StopWidth_BITS_2: flags |= SERIAL_STOP_BITS_2; break;
    }

    switch (config->parity) {
    case fuchsia_hardware_serial_Parity_NONE: flags |= SERIAL_PARITY_NONE; break;
    case fuchsia_hardware_serial_Parity_EVEN: flags |= SERIAL_PARITY_EVEN; break;
    case fuchsia_hardware_serial_Parity_ODD: flags |= SERIAL_PARITY_ODD; break;
    }

    switch (config->control_flow) {
    case fuchsia_hardware_serial_FlowControl_NONE: flags |= SERIAL_FLOW_CTRL_NONE; break;
    case fuchsia_hardware_serial_FlowControl_CTS_RTS: flags |= SERIAL_FLOW_CTRL_CTS_RTS; break;
    }

    zx_status_t status = SerialConfig(config->baud_rate, flags);
    return fuchsia_hardware_serial_DeviceSetConfig_reply(txn, status);
}

zx_status_t SerialDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
    using Binder = fidl::Binder<SerialDevice>;
    static const fuchsia_hardware_serial_Device_ops kOps = {
        .GetClass = Binder::BindMember<&SerialDevice::FidlSerialGetClass>,
        .SetConfig = Binder::BindMember<&SerialDevice::FidlSerialSetConfig>,
    };

    return fuchsia_hardware_serial_Device_dispatch(this, txn, msg, &kOps);
}

void SerialDevice::DdkRelease() {
    serial_.Enable(false);
    serial_.SetNotifyCallback(&kNoCallback);
    event_.reset();
    socket_.reset();
    delete this;
}

zx_status_t SerialDevice::Create(void* ctx, zx_device_t* dev) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<SerialDevice> sdev(new (&ac) SerialDevice(dev));

    if (!ac.check()) {
        zxlogf(ERROR, "SerialDevice::Create: no memory to allocate serial device!\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status;
    if ((status = sdev->Init()) != ZX_OK) {
        return status;
    }

    if ((status = sdev->Bind()) != ZX_OK) {
        zxlogf(ERROR, "SerialDevice::Create: Bind failed\n");
        sdev.release()->DdkRelease();
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = sdev.release();

    return ZX_OK;
}

zx_status_t SerialDevice::Init() {
    if (!serial_.is_valid()) {
        zxlogf(ERROR, "SerialDevice::Init: ZX_PROTOCOL_SERIAL_IMPL not available\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    serial_port_info_t info;
    zx_status_t status = serial_.GetInfo(&info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "SerialDevice::Init: SerialImpl::GetInfo failed %d\n", status);
        return status;
    }
    serial_class_ = info.serial_class;

    return ZX_OK;
}

zx_status_t SerialDevice::Bind() {
    zx_device_prop_t props[] = {
        {BIND_PROTOCOL, 0, ZX_PROTOCOL_SERIAL},
        {BIND_SERIAL_CLASS, 0, serial_class_},
    };

    return DdkAdd("serial", 0, props, fbl::count_of(props));
}

static zx_driver_ops_t serial_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = SerialDevice::Create;
    return ops;
}();

} // namespace serial

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(serial, serial::serial_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SERIAL_IMPL),
ZIRCON_DRIVER_END(serial)
// clang-format on
