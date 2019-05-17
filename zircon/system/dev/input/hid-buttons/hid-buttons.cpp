// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid-buttons.h"

#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <hid/descriptor.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

namespace buttons {

int HidButtonsDevice::Thread() {
    while (1) {
        zx_port_packet_t packet;
        zx_status_t status = port_.wait(zx::time::infinite(), &packet);
        zxlogf(TRACE, "%s msg received on port key %lu\n", __FUNCTION__, packet.key);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s port wait failed %d\n", __FUNCTION__, status);
            return thrd_error;
        }

        if (packet.key == kPortKeyShutDown) {
            zxlogf(INFO, "%s shutting down\n", __FUNCTION__);
            return thrd_success;
        } else if (packet.key >= kPortKeyInterruptStart &&
                   packet.key < (kPortKeyInterruptStart + buttons_.size())) {
            uint32_t type = static_cast<uint32_t>(packet.key - kPortKeyInterruptStart);
            if (gpios_[type].config.type == BUTTONS_GPIO_TYPE_INTERRUPT) {
                // We need to reconfigure the GPIO to catch the opposite polarity.
                ReconfigurePolarity(type, packet.key);
            }

            buttons_input_rpt_t input_rpt;
            size_t out_len;
            status = HidbusGetReport(0, BUTTONS_RPT_ID_INPUT, &input_rpt, sizeof(input_rpt),
                                     &out_len);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s HidbusGetReport failed %d\n", __FUNCTION__, status);
            } else {
                fbl::AutoLock lock(&client_lock_);
                if (client_.is_valid()) {
                    client_.IoQueue(&input_rpt, sizeof(buttons_input_rpt_t));
                    // If report could not be filled, we do not ioqueue.
                }
            }
            if (fdr_gpio_.has_value() && fdr_gpio_.value() == type) {
                zxlogf(INFO, "FDR (up and down buttons) pressed\n");
            }

            gpios_[type].irq.ack();
        }
    }
    return thrd_success;
}

zx_status_t HidButtonsDevice::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
    fbl::AutoLock lock(&client_lock_);
    if (client_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    } else {
        client_ = ddk::HidbusIfcProtocolClient(ifc);
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
    fbl::AutoLock lock(&client_lock_);
    client_.clear();
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

// Requires interrupts to be disabled for all rows/cols.
bool HidButtonsDevice::MatrixScan(uint32_t row, uint32_t col, zx_duration_t delay) {

    gpio_config_in(&gpios_[col].gpio, GPIO_NO_PULL); // Float column to find row in use.
    zx::nanosleep(zx::deadline_after(zx::duration(delay)));

    uint8_t val;
    gpio_read(&gpios_[row].gpio, &val);

    gpio_config_out(&gpios_[col].gpio, gpios_[col].config.output_value);
    zxlogf(TRACE, "%s row %u col %u val %u\n", __FUNCTION__, row, col, val);
    return static_cast<bool>(val);
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

    buttons_input_rpt_t input_rpt = {};
    input_rpt.rpt_id = BUTTONS_RPT_ID_INPUT;

    for (size_t i = 0; i < buttons_.size(); ++i) {
        bool new_value = false; // A value true means a button is pressed.
        if (buttons_[i].type == BUTTONS_TYPE_MATRIX) {
            new_value = MatrixScan(buttons_[i].gpioA_idx, buttons_[i].gpioB_idx,
                                   buttons_[i].gpio_delay);
        } else if (buttons_[i].type == BUTTONS_TYPE_DIRECT) {
            uint8_t val;
            gpio_read(&gpios_[buttons_[i].gpioA_idx].gpio, &val);
            zxlogf(TRACE, "%s GPIO direct read %u for button %lu\n", __FUNCTION__, val, i);
            new_value = val;
        } else {
            zxlogf(ERROR, "%s unknown button type %u\n", __FUNCTION__, buttons_[i].type);
            return ZX_ERR_INTERNAL;
        }

        if (gpios_[i].config.flags & BUTTONS_GPIO_FLAG_INVERTED) {
            new_value = !new_value;
        }

        zxlogf(TRACE, "%s GPIO new value %u for button %lu\n", __FUNCTION__, new_value, i);
        fill_button_in_report(buttons_[i].id, new_value, &input_rpt);
    }
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

void HidButtonsDevice::ReconfigurePolarity(uint32_t idx, uint64_t int_port) {
    zxlogf(TRACE, "%s gpio %u port %lu\n", __FUNCTION__, idx, int_port);
    uint8_t current = 0, old;
    gpio_read(&gpios_[idx].gpio, &current);
    do {
        gpio_set_polarity(&gpios_[idx].gpio, current ? GPIO_POLARITY_LOW : GPIO_POLARITY_HIGH);
        old = current;
        gpio_read(&gpios_[idx].gpio, &current);
        zxlogf(SPEW, "%s old gpio %u new gpio %u\n", __FUNCTION__, old, current);
        // If current switches after setup, we setup a new trigger for it (opposite edge).
    } while (current != old);
}

zx_status_t HidButtonsDevice::ConfigureInterrupt(uint32_t idx, uint64_t int_port) {
    zxlogf(TRACE, "%s gpio %u port %lu\n", __FUNCTION__, idx, int_port);
    zx_status_t status;
    uint8_t current = 0;
    gpio_read(&gpios_[idx].gpio, &current);
    gpio_release_interrupt(&gpios_[idx].gpio);
    // We setup a trigger for the opposite of the current GPIO value.
    status = gpio_get_interrupt(
        &gpios_[idx].gpio,
        current ? ZX_INTERRUPT_MODE_EDGE_LOW : ZX_INTERRUPT_MODE_EDGE_HIGH,
        gpios_[idx].irq.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s gpio_get_interrupt failed %d\n", __FUNCTION__, status);
        return status;
    }
    status = gpios_[idx].irq.bind(port_, int_port, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s zx_interrupt_bind failed %d\n", __FUNCTION__, status);
        return status;
    }
    // To make sure polarity is correct in case it changed during configuration.
    ReconfigurePolarity(idx, int_port);
    return ZX_OK;
}

zx_status_t HidButtonsDevice::Bind() {
    zx_status_t status;

    status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
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

    // Get buttons metadata.
    size_t actual = 0;
    status = device_get_metadata_size(parent_, DEVICE_METADATA_BUTTONS_BUTTONS, &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s device_get_metadata_size failed %d\n", __FILE__, status);
        return ZX_OK;
    }
    size_t n_buttons = actual / sizeof(buttons_button_config_t);
    fbl::AllocChecker ac;
    buttons_ = fbl::Array(new (&ac) buttons_button_config_t[n_buttons], n_buttons);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    actual = 0;
    status = device_get_metadata(parent_, DEVICE_METADATA_BUTTONS_BUTTONS, buttons_.get(),
                                 buttons_.size() * sizeof(buttons_button_config_t), &actual);
    if (status != ZX_OK || actual != buttons_.size() * sizeof(buttons_button_config_t)) {
        zxlogf(ERROR, "%s device_get_metadata failed %d\n", __FILE__, status);
        return status;
    }

    // Get gpios metadata.
    actual = 0;
    status = device_get_metadata_size(parent_, DEVICE_METADATA_BUTTONS_GPIOS, &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s device_get_metadata_size failed %d\n", __FILE__, status);
        return ZX_OK;
    }
    size_t n_gpios = actual / sizeof(buttons_gpio_config_t);
    auto gpios_configs = fbl::Array(new (&ac) buttons_gpio_config_t[n_gpios], n_gpios);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    actual = 0;
    status = device_get_metadata(parent_, DEVICE_METADATA_BUTTONS_GPIOS, gpios_configs.get(),
                                 gpios_configs.size() * sizeof(buttons_gpio_config_t), &actual);
    if (status != ZX_OK || actual != gpios_configs.size() * sizeof(buttons_gpio_config_t)) {
        zxlogf(ERROR, "%s device_get_metadata failed %d\n", __FILE__, status);
        return status;
    }

    // Check the metadata.
    for (uint32_t i = 0; i < buttons_.size(); ++i) {
        if (buttons_[i].gpioA_idx >= gpios_configs.size()) {
            zxlogf(ERROR, "%s invalid gpioA_idx %u\n", __FUNCTION__, buttons_[i].gpioA_idx);
            return ZX_ERR_INTERNAL;
        }
        if (buttons_[i].gpioB_idx >= gpios_configs.size()) {
            zxlogf(ERROR, "%s invalid gpioB_idx %u\n", __FUNCTION__, buttons_[i].gpioB_idx);
            return ZX_ERR_INTERNAL;
        }
        if (gpios_configs[buttons_[i].gpioA_idx].type != BUTTONS_GPIO_TYPE_INTERRUPT) {
            zxlogf(ERROR, "%s invalid gpioA type %u\n", __FUNCTION__,
                   gpios_configs[buttons_[i].gpioA_idx].type);
            return ZX_ERR_INTERNAL;
        }
        if (buttons_[i].type == BUTTONS_TYPE_MATRIX &&
            gpios_configs[buttons_[i].gpioB_idx].type != BUTTONS_GPIO_TYPE_MATRIX_OUTPUT) {
            zxlogf(ERROR, "%s invalid matrix gpioB type %u\n", __FUNCTION__,
                   gpios_configs[buttons_[i].gpioB_idx].type);
            return ZX_ERR_INTERNAL;
        }
        if (buttons_[i].id == BUTTONS_ID_FDR) {
            fdr_gpio_ = buttons_[i].gpioA_idx;
            zxlogf(INFO, "FDR (up and down buttons) setup to GPIO %u\n", *fdr_gpio_);
        }
    }

    // Prepare the GPIOs.
    gpios_ = fbl::Array(new (&ac) Gpio[n_gpios], n_gpios);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    for (uint32_t i = 0; i < gpios_.size(); ++i) {
        gpios_[i].config = gpios_configs[i];
        size_t actual;
        status = PdevGetGpioProtocol(&pdev, i, &gpios_[i].gpio,
                                     sizeof(gpios_[i].gpio), &actual);
        if (status != ZX_OK || actual != sizeof(gpios_[i].gpio)) {
            zxlogf(ERROR, "%s pdev_get_protocol failed %d\n", __FUNCTION__, status);
            return ZX_ERR_NOT_SUPPORTED;
        }
        status = gpio_set_alt_function(&gpios_[i].gpio, 0); // 0 means function GPIO.
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s gpio_set_alt_function failed %d\n", __FUNCTION__, status);
            return ZX_ERR_NOT_SUPPORTED;
        }
        if (gpios_[i].config.type == BUTTONS_GPIO_TYPE_MATRIX_OUTPUT) {
            status = gpio_config_out(&gpios_[i].gpio, gpios_[i].config.output_value);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s gpio_config_out failed %d\n", __FUNCTION__, status);
                return ZX_ERR_NOT_SUPPORTED;
            }
        } else if (gpios_[i].config.type == BUTTONS_GPIO_TYPE_INTERRUPT) {
            status = gpio_config_in(&gpios_[i].gpio, gpios_[i].config.internal_pull);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s gpio_config_in failed %d\n", __FUNCTION__, status);
                return ZX_ERR_NOT_SUPPORTED;
            }
            status = ConfigureInterrupt(i, kPortKeyInterruptStart + i);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    auto f = [](void* arg) -> int { return reinterpret_cast<HidButtonsDevice*>(arg)->Thread(); };
    int rc = thrd_create_with_name(&thread_, f, this, "hid-buttons-thread");
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
    zx_port_packet packet = {kPortKeyShutDown, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
    thrd_join(thread_, NULL);
    for (uint32_t i = 0; i < gpios_.size(); ++i) {
        gpios_[i].irq.destroy();
    }
    fbl::AutoLock lock(&client_lock_);
    client_.clear();
}

void HidButtonsDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void HidButtonsDevice::DdkRelease() {
    delete this;
}

static zx_status_t hid_buttons_bind(void* ctx, zx_device_t* parent) {
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

static zx_driver_ops_t hid_buttons_driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = hid_buttons_bind;
    return ops;
}();

} // namespace buttons

// clang-format off
ZIRCON_DRIVER_BEGIN(hid_buttons, buttons::hid_buttons_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_HID_BUTTONS),
ZIRCON_DRIVER_END(hid_buttons)
// clang-format on
