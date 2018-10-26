// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>

#include <hid/descriptor.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "hid-buttons.h"

// clang-format off
// zx_port_packet::key.
constexpr uint64_t PORT_KEY_SHUTDOWN = 0x01;
// Start of up to kNumberOfRequiredGpios port types used for interrupts.
constexpr uint64_t PORT_KEY_INTERRUPT_START = 0x10;
// clang-format on

namespace buttons {

int HidButtonsDevice::Thread() {
    while (1) {
        zx_port_packet_t packet;
        zx_status_t status = zx_port_wait(port_handle_, ZX_TIME_INFINITE, &packet);
        zxlogf(TRACE, "%s msg received on port key %lu\n", __FUNCTION__, packet.key);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s port wait failed %d\n", __FUNCTION__, status);
            return thrd_error;
        }

        if (packet.key == PORT_KEY_SHUTDOWN) {
            zxlogf(INFO, "%s shutting down\n", __FUNCTION__);
            return thrd_success;
        } else if (packet.key >= PORT_KEY_INTERRUPT_START &&
                   packet.key < (PORT_KEY_INTERRUPT_START + kNumberOfRequiredGpios)) {
            uint32_t type = static_cast<uint32_t>(packet.key - PORT_KEY_INTERRUPT_START);
            keys_[type].irq.ack();
            // We need to reconfigure the GPIO edge detection to catch the opposite direction.
            ReconfigureGpio(type, packet.key);

            buttons_input_rpt_t input_rpt;
            size_t out_len;
            status = HidbusGetReport(0, BUTTONS_RPT_ID_INPUT, &input_rpt, sizeof(input_rpt),
                                     &out_len);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s HidbusGetReport failed %d\n", __FUNCTION__, status);
            } else {
                fbl::AutoLock lock(&proxy_lock_);
                if (proxy_.is_valid()) {
                    proxy_.IoQueue(&input_rpt, sizeof(buttons_input_rpt_t));
                    // If report could not be filled, we do not ioqueue.
                }
            }
            if (packet.key == PORT_KEY_INTERRUPT_START + kGpioVolumeUpDown) {
                zxlogf(INFO, "FDR (up and down buttons) pressed\n");
            }
        }
    }
    return thrd_success;
}

zx_status_t HidButtonsDevice::HidbusStart(const hidbus_ifc_t* ifc) {
    fbl::AutoLock lock(&proxy_lock_);
    if (proxy_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    } else {
        proxy_ = ddk::HidbusIfcProxy(ifc);
    }
    return ZX_OK;
}

zx_status_t HidButtonsDevice::HidbusQuery(uint32_t options, hid_info_t* info) {
    if (!info) {
        return ZX_ERR_INVALID_ARGS;
    }
    info->dev_num = 0;
    info->device_class = HID_DEVICE_CLASS_OTHER;
    info->boot_device = false;

    return ZX_OK;
}

void HidButtonsDevice::HidbusStop() {
    fbl::AutoLock lock(&proxy_lock_);
    proxy_.clear();
}

zx_status_t HidButtonsDevice::HidbusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    const uint8_t* desc_ptr;
    uint8_t* buf;
    if (!len || !data) {
        return ZX_ERR_INVALID_ARGS;
    }
    *len = get_buttons_report_desc(&desc_ptr);
    fbl::AllocChecker ac;
    buf = new (&ac) uint8_t[*len];
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(buf, desc_ptr, *len);
    *data = buf;
    return ZX_OK;
}

zx_status_t HidButtonsDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                              size_t len, size_t* out_len) {
    if (!data || !out_len) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (rpt_id != BUTTONS_RPT_ID_INPUT) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    *out_len = sizeof(buttons_input_rpt_t);
    if (*out_len > len) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    buttons_input_rpt_t input_rpt;
    input_rpt.rpt_id = BUTTONS_RPT_ID_INPUT;
    input_rpt.volume = 0;
    input_rpt.padding = 0;
    uint8_t val;
    gpio_read(&keys_[kGpioVolumeUp].gpio, &val);
    if (!val) { // Up button is pressed down.
        input_rpt.volume = 1;
    }
    gpio_read(&keys_[kGpioVolumeDown].gpio, &val);
    if (!val) {               // Down button is pressed down.
        input_rpt.volume = 3; // -1 for 2 bits.
    }
    gpio_read(&keys_[kGpioMicPrivacy].gpio, &val);
    input_rpt.mute = static_cast<bool>(val);
    auto out = static_cast<buttons_input_rpt_t*>(data);
    *out = input_rpt;

    return ZX_OK;
}

zx_status_t HidButtonsDevice::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                              size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidButtonsDevice::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidButtonsDevice::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidButtonsDevice::HidbusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t HidButtonsDevice::HidbusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}

zx_status_t HidButtonsDevice::ReconfigureGpio(uint32_t idx, uint64_t int_port) {
    zx_status_t status;
    uint8_t current = 0, old;
    gpio_read(&keys_[idx].gpio, &current);
    do {
        gpio_release_interrupt(&keys_[idx].gpio);
        // We setup a trigger for the opposite of the current GPIO value.
        status = gpio_get_interrupt(&keys_[idx].gpio, current ? ZX_INTERRUPT_MODE_EDGE_LOW :
                                    ZX_INTERRUPT_MODE_EDGE_HIGH,
                                    keys_[idx].irq.reset_and_get_address());
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s gpio_get_interrupt failed %d\n", __FUNCTION__, status);
            return status;
        }
        old = current;
        gpio_read(&keys_[idx].gpio, &current);
        zxlogf(SPEW, "%s old gpio %u new gpio %u\n", __FUNCTION__, old, current);
        // If current switches after setup, we setup a new trigger for it (opposite edge).
    } while (current != old);
    status = keys_[idx].irq.bind(port_handle_, int_port, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s zx_interrupt_bind failed %d\n", __FUNCTION__, status);
        return status;
    }
    return ZX_OK;
}

zx_status_t HidButtonsDevice::Bind() {
    zx_status_t status;

    status = zx_port_create(ZX_PORT_BIND_TO_INTERRUPT, &port_handle_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s port_create failed %d\n", __FUNCTION__, status);
        return status;
    }

    pdev_protocol_t pdev;
    status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s device_get_protocol failed %d\n", __FUNCTION__, status);
        return status;
    }

    pdev_device_info_t pdev_info;
    status = pdev_get_device_info(&pdev, &pdev_info);
    if (pdev_info.gpio_count != kNumberOfRequiredGpios) {
        zxlogf(ERROR, "%s Incorrect number of GPIOs configured: %u (%u needed)\n", __FUNCTION__,
               pdev_info.gpio_count, kNumberOfRequiredGpios);
        return ZX_ERR_NOT_SUPPORTED;
    }

    // TODO(andresoportus): Make what GPIOs are required variable per board's metadata.
    fbl::AllocChecker ac;
    keys_ = fbl::Array(new (&ac) GpioKeys[kNumberOfRequiredGpios], kNumberOfRequiredGpios);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    for (uint32_t i = 0; i < kNumberOfRequiredGpios; ++i) {
        size_t actual;
        status = pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, i, &keys_[i].gpio,
                                   sizeof(keys_[i].gpio), &actual);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s pdev_get_protocol failed %d\n", __FUNCTION__, status);
            return ZX_ERR_NOT_SUPPORTED;
        }
        status = gpio_config_in(&keys_[i].gpio, GPIO_NO_PULL);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s gpio_config_in failed %d\n", __FUNCTION__, status);
            return ZX_ERR_NOT_SUPPORTED;
        }
        status = ReconfigureGpio(i, PORT_KEY_INTERRUPT_START + i);
        if (status != ZX_OK) {
            return status;
        }
    }

    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<HidButtonsDevice*>(arg)->Thread();
                                   },
                                   this,
                                   "hid-buttons-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    status = DdkAdd("hid-buttons");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s DdkAdd failed %d\n", __FUNCTION__, status);
        ShutDown();
        return status;
    }

    return ZX_OK;
}

void HidButtonsDevice::ShutDown() {
    zx_port_packet packet = {PORT_KEY_SHUTDOWN, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = zx_port_queue(port_handle_, &packet);
    ZX_ASSERT(status == ZX_OK);
    thrd_join(thread_, NULL);
    for (uint32_t i = 0; i < kNumberOfRequiredGpios; ++i) {
        keys_[i].irq.destroy();
    }
    fbl::AutoLock lock(&proxy_lock_);
    proxy_.clear();
}

void HidButtonsDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void HidButtonsDevice::DdkRelease() {
    delete this;
}

} // namespace buttons

extern "C" zx_status_t hid_buttons_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<buttons::HidButtonsDevice>(&ac, parent);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev.
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
