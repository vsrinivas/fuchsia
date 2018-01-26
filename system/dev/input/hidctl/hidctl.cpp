// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hidctl.h"

#include <ddk/debug.h>
#include <zircon/compiler.h>
#include <fbl/auto_lock.h>
#include <fbl/type_support.h>
#include <pretty/hexdump.h>

#include <stdio.h>
#include <string.h>

namespace hidctl {

HidCtl::HidCtl(zx_device_t* device) : ddk::Device<HidCtl, ddk::Ioctlable>(device) {}

void HidCtl::DdkRelease() {
    delete this;
}

zx_status_t HidCtl::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                             size_t out_len, size_t* out_actual) {
    switch (op) {
    case IOCTL_HIDCTL_CONFIG: {
        if (in_buf == nullptr || in_len < sizeof(hid_ioctl_config_t) ||
            out_buf == nullptr || out_len != sizeof(zx_handle_t) || out_actual == nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }

        auto config = static_cast<const hid_ioctl_config*>(in_buf);
        if (in_len != sizeof(hid_ioctl_config_t) + config->rpt_desc_len) {
            return ZX_ERR_INVALID_ARGS;
        }

        if (config->dev_class > HID_DEV_CLASS_LAST) {
            return ZX_ERR_INVALID_ARGS;
        }


        zx::socket local, remote;
        zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote);
        if (status != ZX_OK) {
            return status;
        }


        auto hiddev = fbl::unique_ptr<hidctl::HidDevice>(
                new hidctl::HidDevice(zxdev(), config, fbl::move(local)));

        status = hiddev->DdkAdd("hidctl-dev");
        if (status != ZX_OK) {
            zxlogf(ERROR, "hidctl: could not add hid device: %d\n", status);
            hiddev->Shutdown();
        } else {
            // devmgr owns the memory until release is called
            __UNUSED auto ptr = hiddev.release();

            auto out = static_cast<zx_handle_t*>(out_buf);
            *out = remote.release();
            *out_actual = sizeof(zx_handle_t);
            zxlogf(INFO, "hidctl: created hid device\n");
        }
        return status;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

int hid_device_thread(void* arg) {
    HidDevice* device = reinterpret_cast<HidDevice*>(arg);
    return device->Thread();
}

#define HID_SHUTDOWN ZX_USER_SIGNAL_7

HidDevice::HidDevice(zx_device_t* device, const hid_ioctl_config* config, zx::socket data)
  : ddk::Device<HidDevice, ddk::Unbindable>(device),
    boot_device_(config->boot_device),
    dev_class_(config->dev_class),
    report_desc_(new uint8_t[config->rpt_desc_len]),
    report_desc_len_(config->rpt_desc_len),
    data_(fbl::move(data)) {
    ZX_DEBUG_ASSERT(data_.is_valid());
    memcpy(report_desc_.get(), config->rpt_desc, report_desc_len_);
    int ret = thrd_create_with_name(&thread_, hid_device_thread, reinterpret_cast<void*>(this),
                                    "hidctl-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);
}

void HidDevice::DdkRelease() {
    zxlogf(TRACE, "hidctl: DdkRelease\n");
    // Only the thread will call DdkRemove() when the loop exits. This detachs the thread before it
    // exits, so no need to join.
    delete this;
}

void HidDevice::DdkUnbind() {
    zxlogf(TRACE, "hidctl: DdkUnbind\n");
    Shutdown();
    // The thread will call DdkRemove when it exits the loop.
}

zx_status_t HidDevice::HidBusQuery(uint32_t options, hid_info_t* info) {
    zxlogf(TRACE, "hidctl: query\n");

    info->dev_num = 0;
    info->dev_class = dev_class_;
    info->boot_device = boot_device_;
    return ZX_OK;
}

zx_status_t HidDevice::HidBusStart(ddk::HidBusIfcProxy proxy) {
    zxlogf(TRACE, "hidctl: start\n");

    fbl::AutoLock lock(&lock_);
    if (proxy_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    }
    proxy_ = proxy;
    return ZX_OK;
}

void HidDevice::HidBusStop() {
    zxlogf(TRACE, "hidctl: stop\n");

    fbl::AutoLock lock(&lock_);
    proxy_.clear();
}

zx_status_t HidDevice::HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    zxlogf(TRACE, "hidctl: get descriptor %u\n", desc_type);

    if (data == nullptr || len == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (desc_type != HID_DESC_TYPE_REPORT) {
        return ZX_ERR_NOT_FOUND;
    }

    *data = malloc(report_desc_len_);
    if (*data == nullptr) {
        return ZX_ERR_NO_MEMORY;
    }
    *len = report_desc_len_;
    memcpy(*data, report_desc_.get(), report_desc_len_);
    return ZX_OK;
}

zx_status_t HidDevice::HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                            size_t* out_len) {
    zxlogf(TRACE, "hidctl: get report type=%u id=%u\n", rpt_type, rpt_id);

    if (out_len == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    // TODO: send get report message over socket
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len) {
    zxlogf(TRACE, "hidctl: set report type=%u id=%u\n", rpt_type, rpt_id);

    // TODO: send set report message over socket
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidBusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    zxlogf(TRACE, "hidctl: get idle\n");

    // TODO: send get idle message over socket
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidBusSetIdle(uint8_t rpt_id, uint8_t duration) {
    zxlogf(TRACE, "hidctl: set idle\n");

    // TODO: send set idle message over socket
    return ZX_OK;
}

zx_status_t HidDevice::HidBusGetProtocol(uint8_t* protocol) {
    zxlogf(TRACE, "hidctl: get protocol\n");

    // TODO: send get protocol message over socket
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidDevice::HidBusSetProtocol(uint8_t protocol) {
    zxlogf(TRACE, "hidctl: set protocol\n");

    // TODO: send set protocol message over socket
    return ZX_OK;
}

int HidDevice::Thread() {
    zxlogf(TRACE, "hidctl: starting main thread\n");
    zx_signals_t pending;
    fbl::unique_ptr<uint8_t[]> buf(new uint8_t[mtu_]);

    zx_status_t status = ZX_OK;
    const zx_signals_t wait = ZX_SOCKET_READABLE | ZX_SOCKET_PEER_CLOSED | HID_SHUTDOWN;
    while (true) {
        status = data_.wait_one(wait, zx::time::infinite(), &pending);
        if (status != ZX_OK) {
            zxlogf(ERROR, "hidctl: error waiting on data: %d\n", status);
            break;
        }

        if (pending & ZX_SOCKET_READABLE) {
            status = Recv(buf.get(), mtu_);
            if (status != ZX_OK) {
                break;
            }
        }
        if (pending & ZX_SOCKET_PEER_CLOSED) {
            zxlogf(TRACE, "hidctl: socket closed (peer)\n");
            break;
        }
        if (pending & HID_SHUTDOWN) {
            zxlogf(TRACE, "hidctl: socket closed (self)\n");
            break;
        }
    }

    zxlogf(INFO, "hidctl: device destroyed\n");
    {
        fbl::AutoLock lock(&lock_);
        data_.reset();
        thrd_detach(thread_);
    }
    DdkRemove();

    return static_cast<int>(status);
}

void HidDevice::Shutdown() {
    fbl::AutoLock lock(&lock_);
    if (data_.is_valid()) {
        // Prevent further writes to the socket
        zx_status_t status = data_.write(ZX_SOCKET_SHUTDOWN_READ, nullptr, 0, nullptr);
        ZX_DEBUG_ASSERT(status == ZX_OK);
        // Signal the thread to shutdown
        status = data_.signal(0, HID_SHUTDOWN);
        ZX_DEBUG_ASSERT(status == ZX_OK);
    }
}

zx_status_t HidDevice::Recv(uint8_t* buffer, uint32_t capacity) {
    size_t actual = 0;
    zx_status_t status = ZX_OK;
    // Read all the datagrams out of the socket.
    while (status == ZX_OK) {
        status = data_.read(0u, buffer, capacity, &actual);
        if (status == ZX_ERR_SHOULD_WAIT || status == ZX_ERR_PEER_CLOSED) {
            break;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "hidctl: error reading data: %d\n", status);
            return status;
        }

        fbl::AutoLock lock(&lock_);
        if (unlikely(driver_get_log_flags() & DDK_LOG_TRACE)) {
            zxlogf(TRACE, "hidctl: received %zu bytes\n", actual);
            hexdump8_ex(buffer, actual, 0);
        }
        if (proxy_.is_valid()) {
            proxy_.IoQueue(buffer, actual);
        }
    }
    return ZX_OK;
}

}  // namespace hidctl

extern "C" zx_status_t hidctl_bind(void* ctx, zx_device_t* device, void** cookie) {
    auto dev = fbl::unique_ptr<hidctl::HidCtl>(new hidctl::HidCtl(device));
    zx_status_t status = dev->DdkAdd("hidctl");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not add device: %d\n", __func__, status);
    } else {
        // devmgr owns the memory now
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
