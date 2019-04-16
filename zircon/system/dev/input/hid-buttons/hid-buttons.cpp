// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>

#include <fuchsia/device/manager/c/fidl.h>

#include <hid/descriptor.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/zx/channel.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "hid-buttons.h"

// clang-format off
// zx_port_packet::key.
constexpr uint64_t PORT_KEY_SHUTDOWN = 0x01;
// Start of up to kNumberOfRequiredGpios port types used for interrupts.
constexpr uint64_t PORT_KEY_INTERRUPT_START = 0x10;
// clang-format on

namespace {

// The signal to terminate the thread responsible for rebooting.
constexpr zx_signals_t kSignalRebootTerminate = ZX_USER_SIGNAL_0;

zx_status_t send_reboot() {
    zx::channel channel_local, channel_remote;
    zx_status_t status = zx::channel::create(0, &channel_local, &channel_remote);
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to create channel: %d\n", status);
        return ZX_ERR_INTERNAL;
    }

    const char* service = "/svc/" fuchsia_device_manager_Administrator_Name;
    status = fdio_service_connect(service, channel_remote.get());
    if (status != ZX_OK) {
        zxlogf(ERROR, "failed to connect to service %s: %d\n", service, status);
        return ZX_ERR_INTERNAL;
    }

    zx_status_t call_status;
    status = fuchsia_device_manager_AdministratorSuspend(channel_local.get(),
                                                         DEVICE_SUSPEND_FLAG_REBOOT,
                                                         &call_status);
    if (status != ZX_OK || call_status != ZX_OK) {
        zxlogf(ERROR, "Call to %s failed: ret: %d  remote: %d\n", service, status, call_status);
        return status != ZX_OK ? status : call_status;
    }

    return ZX_OK;
}

} // namespace

namespace buttons {

int HidButtonsDevice::RebootThread() {
    while (1) {
        zx_signals_t signals;
        zx_status_t status = reboot_timer_.wait_one(ZX_TIMER_SIGNALED | kSignalRebootTerminate,
                                                    zx::time::infinite(), &signals);
        reboot_running_ = false;

        if (status == ZX_ERR_CANCELED) {
            continue;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s reboot_timer wait failed with with status %d\n", __FUNCTION__,
                   status);
            continue;
        }
        if (signals & kSignalRebootTerminate) {
            return 0;
        }

        status = send_reboot();
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s Reboot failed with status %d\n", __FUNCTION__, status);
            continue;
        }
    }
}

int HidButtonsDevice::Thread() {
    while (1) {
        zx_port_packet_t packet;
        zx_status_t status = port_.wait(zx::time::infinite(), &packet);
        zxlogf(TRACE, "%s msg received on port key %lu\n", __FUNCTION__, packet.key);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s port wait failed %d\n", __FUNCTION__, status);
            return thrd_error;
        }

        if (packet.key == PORT_KEY_SHUTDOWN) {
            zxlogf(INFO, "%s shutting down\n", __FUNCTION__);
            return thrd_success;
        } else if (packet.key >= PORT_KEY_INTERRUPT_START &&
                   packet.key < (PORT_KEY_INTERRUPT_START + buttons_.size())) {
            uint32_t type = static_cast<uint32_t>(packet.key - PORT_KEY_INTERRUPT_START);
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

zx_status_t HidButtonsDevice::UpdateReboot(uint8_t button_id, bool pressed) {
    if (button_id != reboot_button_) {
        return ZX_OK;
    }
    if (pressed && !reboot_running_) {
        zx_status_t status = reboot_timer_.set(
            zx::time(zx_deadline_after(ZX_MSEC(reboot_mseconds_delay_))),
            zx::duration(ZX_MSEC(100)));
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s reboot timer set failed %d\n", __FUNCTION__, status);
            return status;
        }
        reboot_running_ = true;
    }
    if (!pressed) {
        zx_status_t status = reboot_timer_.cancel();
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s reboot timer cancel failed %d\n", __FUNCTION__, status);
            return status;
        }
        reboot_running_ = false;
    }
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

    buttons_input_rpt_t input_rpt = {};
    input_rpt.rpt_id = BUTTONS_RPT_ID_INPUT;

    // Disable interrupts.
    for (uint32_t i = 0; i < gpios_.size(); ++i) {
        if (gpios_[i].config.type == BUTTONS_GPIO_TYPE_INTERRUPT) {
            zx_status_t status = gpio_release_interrupt(&gpios_[i].gpio);
            if (status != ZX_OK) {
                zxlogf(ERROR, "%s gpio_release_interrupt failed %d\n", __FUNCTION__, status);
                return status;
            }
        }
    }

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

        if (reboot_button_ != BUTTONS_ID_MAX) {
            zx_status_t status = UpdateReboot(buttons_[i].id, new_value);
            if (status != ZX_OK) {
                return status;
            }
        }
    }
    auto out = static_cast<buttons_input_rpt_t*>(data);
    *out = input_rpt;

    // Reenable interrupts.
    for (uint32_t i = 0; i < gpios_.size(); ++i) {
        if (gpios_[i].config.type == BUTTONS_GPIO_TYPE_INTERRUPT) {
            zx_status_t status = ConfigureInterrupt(i, PORT_KEY_INTERRUPT_START + i);
            if (status != ZX_OK) {
                return status;
            }
        }
    }
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

    pdev_device_info_t pdev_info;
    status = pdev_get_device_info(&pdev, &pdev_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s pdev_get_device_info failed %d\n", __FUNCTION__, status);
        return status;
    }

    // TODO(andresoportus): Remove BUTTONS_ID_MAX usage below once we add metadata size probe
    // capability to devmgr.

    fbl::AllocChecker ac;
    // We have up to BUTTONS_ID_MAX available buttons.
    auto buttons = fbl::Array(new (&ac) buttons_button_config_t[BUTTONS_ID_MAX], BUTTONS_ID_MAX);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    size_t actual = 0;
    status = device_get_metadata(parent_, DEVICE_METADATA_BUTTONS_BUTTONS, buttons.get(),
                                 buttons.size() * sizeof(buttons_button_config_t), &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s device_get_metadata failed %d\n", __FILE__, status);
        return status;
    }
    size_t n_buttons = actual / sizeof(buttons_button_config_t);

    // We have up to BUTTONS_ID_MAX available gpios.
    auto gpios_configs = fbl::Array(
        new (&ac) buttons_gpio_config_t[BUTTONS_ID_MAX], BUTTONS_ID_MAX);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    actual = 0;
    status = device_get_metadata(parent_, DEVICE_METADATA_BUTTONS_GPIOS, gpios_configs.get(),
                                 gpios_configs.size() * sizeof(buttons_gpio_config_t), &actual);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s device_get_metadata failed %d\n", __FILE__, status);
        return status;
    }
    size_t n_gpios = actual / sizeof(buttons_gpio_config_t);

    buttons_ = fbl::Array(new (&ac) buttons_button_config_t[n_buttons], n_buttons);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    gpios_ = fbl::Array(new (&ac) Gpio[n_gpios], n_gpios);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    for (uint32_t i = 0; i < buttons_.size(); ++i) {
        buttons_[i] = buttons[i];
        if (buttons_[i].gpioA_idx >= gpios_.size()) {
            zxlogf(ERROR, "%s invalid gpioA_idx %u\n", __FUNCTION__, buttons_[i].gpioA_idx);
            return ZX_ERR_INTERNAL;
        }
        if (buttons_[i].gpioB_idx >= gpios_.size()) {
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

    for (uint32_t i = 0; i < gpios_.size(); ++i) {
        gpios_[i].config = gpios_configs[i];
        size_t actual;
        status = pdev_get_protocol(&pdev, ZX_PROTOCOL_GPIO, i, &gpios_[i].gpio,
                                   sizeof(gpios_[i].gpio), &actual);
        if (status != ZX_OK) {
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
            status = ConfigureInterrupt(i, PORT_KEY_INTERRUPT_START + i);
            if (status != ZX_OK) {
                return status;
            }
        }
    }

    // Setup reboot config if it exists.
    buttons_reboot_config_t reboot_config;
    actual = 0;
    status = device_get_metadata(parent_, DEVICE_METADATA_BUTTONS_REBOOT, &reboot_config,
                                 sizeof(buttons_reboot_config_t), &actual);
    if (status == ZX_OK) {
        if (actual != sizeof(buttons_reboot_config_t)) {
            zxlogf(ERROR, "%s getting reboot config size (%ld) is not equal to"
                          "expected size (%ld)\n",
                   __FILE__, actual, sizeof(buttons_reboot_config_t));
            return ZX_ERR_INTERNAL;
        }
        reboot_button_ = reboot_config.button_id;
        reboot_mseconds_delay_ = reboot_config.mseconds_delay;
        status = zx::timer::create(0, ZX_CLOCK_MONOTONIC, &reboot_timer_);
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s zx_timer_create failed %d\n", __FUNCTION__, status);
            return status;
        }

        auto f = [](void* arg) -> int {
            return reinterpret_cast<HidButtonsDevice*>(arg)->RebootThread();
        };

        int rc = thrd_create_with_name(&reboot_thread_, f, this, "hid-buttons-reboot-thread");
        if (rc != thrd_success) {
            return ZX_ERR_INTERNAL;
        }
    }

    auto f = [](void* arg) -> int { return reinterpret_cast<HidButtonsDevice*>(arg)->Thread(); };
    int rc = thrd_create_with_name(&thread_, f, this, "hid-buttons-thread");
    if (rc != thrd_success) {
        if (reboot_button_ != BUTTONS_ID_MAX) {
            reboot_timer_.signal(0, kSignalRebootTerminate);
            thrd_join(reboot_thread_, NULL);
        }
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
    zx_status_t status = port_.queue(&packet);
    ZX_ASSERT(status == ZX_OK);
    thrd_join(thread_, NULL);

    if (reboot_button_ != BUTTONS_ID_MAX) {
        reboot_timer_.signal(0, kSignalRebootTerminate);
        thrd_join(reboot_thread_, NULL);
    }

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
