// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>

#include <hid/descriptor.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include <zircon/device/input.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include "tcs3400-regs.h"
#include "tcs3400.h"

constexpr uint32_t TCS3400_INTERRUPT_IDX = 0;
constexpr zx_duration_t INTERRUPTS_HYSTERESIS = ZX_MSEC(100);
constexpr uint8_t SAMPLES_TO_TRIGGER = 0x01;

#define GET_BYTE(val, shift) static_cast<uint8_t>((val >> shift) & 0xFF)

// clang-format off
// zx_port_packet::type
#define TCS_SHUTDOWN  0x01
#define TCS_CONFIGURE 0x02
#define TCS_INTERRUPT 0x03
#define TCS_REARM_IRQ 0x04
#define TCS_POLL      0x05
// clang-format on

namespace tcs {

zx_status_t Tcs3400Device::FillInputRpt() {
    input_rpt_.rpt_id = AMBIENT_LIGHT_RPT_ID_INPUT;
    struct Regs {
        uint16_t* out;
        uint8_t reg_h;
        uint8_t reg_l;
    } regs[] = {
        {&input_rpt_.illuminance, TCS_I2C_CDATAH, TCS_I2C_CDATAL},
        {&input_rpt_.red, TCS_I2C_RDATAH, TCS_I2C_RDATAL},
        {&input_rpt_.green, TCS_I2C_GDATAH, TCS_I2C_GDATAL},
        {&input_rpt_.blue, TCS_I2C_BDATAH, TCS_I2C_BDATAL},
    };
    for (const auto& i : regs) {
        uint8_t buf_h, buf_l;
        zx_status_t status;
        fbl::AutoLock lock(&i2c_lock_);
        // Read lower byte first, the device holds upper byte of a sample in a shadow register after
        // a lower byte read
        status = i2c_transact_sync(&i2c_, kI2cIndex, &i.reg_l, 1, &buf_l, 1);
        if (status != ZX_OK) {
            zxlogf(ERROR, "Tcs3400Device::FillInputRpt: i2c_transact_sync failed: %d\n", status);
            input_rpt_.state = HID_USAGE_SENSOR_STATE_ERROR_VAL;
            return status;
        }
        status = i2c_transact_sync(&i2c_, kI2cIndex, &i.reg_h, 1, &buf_h, 1);
        if (status != ZX_OK) {
            zxlogf(ERROR, "Tcs3400Device::FillInputRpt: i2c_transact_sync failed: %d\n", status);
            input_rpt_.state = HID_USAGE_SENSOR_STATE_ERROR_VAL;
            return status;
        }
        *i.out = static_cast<uint16_t>(((buf_h & 0xFF) << 8) | (buf_l & 0xFF));
    }
    input_rpt_.state = HID_USAGE_SENSOR_STATE_READY_VAL;
    return ZX_OK;
}

int Tcs3400Device::Thread() {
    // Both polling and interrupts are supported simultaneously
    zx_time_t poll_timeout = ZX_TIME_INFINITE;
    zx_time_t irq_rearm_timeout = ZX_TIME_INFINITE;
    while (1) {
        zx_port_packet_t packet;
        zx_time_t timeout = fbl::min(poll_timeout, irq_rearm_timeout);
        zx_status_t status = zx_port_wait(port_handle_, timeout, &packet);
        if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
            zxlogf(ERROR, "Tcs3400Device::Thread: port wait failed: %d\n", status);
            return thrd_error;
        }

        if (status == ZX_ERR_TIMED_OUT) {
            if (timeout == irq_rearm_timeout) {
                packet.key = TCS_REARM_IRQ;
            } else {
                packet.key = TCS_POLL;
            }
        }

        uint16_t threshold_low;
        uint16_t threshold_high;

        switch (packet.key) {
        case TCS_SHUTDOWN:
            zxlogf(INFO, "Tcs3400Device::Thread: shutting down\n");
            return thrd_success;
        case TCS_CONFIGURE:
            {
                fbl::AutoLock lock(&feature_lock_);
                threshold_low = feature_rpt_.threshold_low;
                threshold_high = feature_rpt_.threshold_high;
                if (feature_rpt_.interval_ms == 0) { // per spec 0 is device's default
                    poll_timeout = ZX_TIME_INFINITE; // we define the default as no polling
                } else {
                    poll_timeout = zx_deadline_after(ZX_MSEC(feature_rpt_.interval_ms));
                }
            }
            {
                struct Setup {
                    uint8_t cmd;
                    uint8_t val;
                } __PACKED setup[] = {
                    {TCS_I2C_ENABLE, TCS_I2C_ENABLE_POWER_ON | TCS_I2C_ENABLE_ADC_ENABLE |
                                         TCS_I2C_ENABLE_INT_ENABLE},
                    {TCS_I2C_AILTL, GET_BYTE(threshold_low, 0)},
                    {TCS_I2C_AILTH, GET_BYTE(threshold_low, 8)},
                    {TCS_I2C_AIHTL, GET_BYTE(threshold_high, 0)},
                    {TCS_I2C_AIHTH, GET_BYTE(threshold_high, 8)},
                    {TCS_I2C_PERS, SAMPLES_TO_TRIGGER},
                };
                for (const auto& i : setup) {
                    fbl::AutoLock lock(&i2c_lock_);
                    status = i2c_transact_sync(&i2c_, kI2cIndex, &i.cmd, sizeof(setup[0]), NULL, 0);
                    if (status != ZX_OK) {
                        zxlogf(ERROR, "Tcs3400Device::Thread: i2c_transact_sync failed: %d\n",
                               status);
                        break; // do not exit thread, future transactions may succeed
                    }
                }
            }
            break;
        case TCS_INTERRUPT:
            zx_interrupt_ack(irq_.get()); // rearm interrupt at the IRQ level
            {
                fbl::AutoLock lock(&feature_lock_);
                threshold_low = feature_rpt_.threshold_low;
                threshold_high = feature_rpt_.threshold_high;
            }
            {
                fbl::AutoLock lock(&proxy_input_lock_);
                if (FillInputRpt() == ZX_OK) {
                    if (input_rpt_.illuminance > threshold_high) {
                        input_rpt_.event =
                            HID_USAGE_SENSOR_EVENT_HIGH_THRESHOLD_CROSS_UPWARD_VAL;
                        proxy_.IoQueue(reinterpret_cast<uint8_t*>(&input_rpt_),
                                       sizeof(ambient_light_input_rpt_t));
                    } else if (input_rpt_.illuminance < threshold_low) {
                        input_rpt_.event =
                            HID_USAGE_SENSOR_EVENT_LOW_THRESHOLD_CROSS_DOWNWARD_VAL;
                        proxy_.IoQueue(reinterpret_cast<uint8_t*>(&input_rpt_),
                                       sizeof(ambient_light_input_rpt_t));
                    }
                }
                // If report could not be filled, we do not ioqueue
                irq_rearm_timeout = zx_deadline_after(INTERRUPTS_HYSTERESIS);
            }
            break;
        case TCS_REARM_IRQ:
            // rearm interrupt at the device level
            {
                fbl::AutoLock lock(&i2c_lock_);
                uint8_t cmd[] = {TCS_I2C_AICLEAR, 0x00};
                status = i2c_transact_sync(&i2c_, kI2cIndex, &cmd, sizeof(cmd), NULL, 0);
                if (status != ZX_OK) {
                    zxlogf(ERROR, "Tcs3400Device::Thread: i2c_transact_sync failed: %d\n",
                           status);
                    // Continue on error, future transactions may succeed
                }
                irq_rearm_timeout = ZX_TIME_INFINITE;
            }
            break;
        case TCS_POLL:
            {
                fbl::AutoLock lock(&proxy_input_lock_);
                if (proxy_.is_valid()) {
                    FillInputRpt(); // We ioqueue even if report filling failed reporting bad state
                    input_rpt_.event = HID_USAGE_SENSOR_EVENT_PERIOD_EXCEEDED_VAL;
                    proxy_.IoQueue(reinterpret_cast<uint8_t*>(&input_rpt_),
                                   sizeof(ambient_light_input_rpt_t));
                }
            }
            {
                fbl::AutoLock lock(&feature_lock_);
                poll_timeout += ZX_MSEC(feature_rpt_.interval_ms);
                zx_time_t now = zx_clock_get_monotonic();
                if (now > poll_timeout) {
                    poll_timeout = zx_deadline_after(ZX_MSEC(feature_rpt_.interval_ms));
                }
            }
            break;
        }
    }
    return thrd_success;
}

zx_status_t Tcs3400Device::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
    if (count == 0) {
        return ZX_OK;
    }
    uint8_t* p = reinterpret_cast<uint8_t*>(buf);
    uint8_t addr;
    zx_status_t status;
    fbl::AutoLock lock(&i2c_lock_);

    // Read lower byte first, the device holds upper byte of a sample in a shadow register after a
    // lower byte read
    addr = TCS_I2C_CDATAL;
    status = i2c_transact_sync(&i2c_, kI2cIndex, &addr, 1, p + 1, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::DdkRead: i2c_transact_sync failed: %d\n", status);
        return status;
    }
    if (count == 1) {
        zxlogf(INFO, "TCS-3400 clear light read: 0x%02X\n", *p);
        *actual = 1;
        return ZX_OK;
    }
    addr = TCS_I2C_CDATAH;
    status = i2c_transact_sync(&i2c_, kI2cIndex, &addr, 1, p, 1);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::DdkRead: i2c_transact_sync failed: %d\n", status);
        return status;
    }
    zxlogf(INFO, "TCS-3400 clear light read: 0x%02X%02X\n", *p, *(p + 1));
    *actual = 2;
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusStart(ddk::HidBusIfcProxy proxy) {
    fbl::AutoLock lock(&proxy_input_lock_);
    if (proxy_.is_valid()) {
        return ZX_ERR_ALREADY_BOUND;
    } else {
        proxy_ = proxy;
    }
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusQuery(uint32_t options, hid_info_t* info) {
    if (!info) {
        return ZX_ERR_INVALID_ARGS;
    }
    info->dev_num = 0;
    info->dev_class = HID_DEV_CLASS_OTHER;
    info->boot_device = false;

    return ZX_OK;
}

void Tcs3400Device::HidBusStop() {
}

zx_status_t Tcs3400Device::HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {
    const uint8_t* desc_ptr;
    uint8_t* buf;
    *len = get_ambient_light_report_desc(&desc_ptr);
    fbl::AllocChecker ac;
    buf = new (&ac) uint8_t[*len];
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(buf, desc_ptr, *len);
    *data = buf;
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                           size_t len, size_t* out_len) {
    if (rpt_id != AMBIENT_LIGHT_RPT_ID_INPUT && rpt_id != AMBIENT_LIGHT_RPT_ID_FEATURE) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    *out_len = (rpt_id == AMBIENT_LIGHT_RPT_ID_INPUT) ?
        sizeof(ambient_light_input_rpt_t) : sizeof(ambient_light_feature_rpt_t);
    if (*out_len > len) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    if (rpt_id == AMBIENT_LIGHT_RPT_ID_INPUT) {
        fbl::AutoLock lock(&proxy_input_lock_);
        FillInputRpt();
        auto out = static_cast<ambient_light_input_rpt_t*>(data);
        *out = input_rpt_; // TA doesn't work on a memcpy taking an address as in &input_rpt_
    } else {
        fbl::AutoLock lock(&feature_lock_);
        auto out = static_cast<ambient_light_feature_rpt_t*>(data);
        *out = feature_rpt_; // TA doesn't work on a memcpy taking an address as in &feature_rpt_
    }
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                           size_t len) {
    if (rpt_id != AMBIENT_LIGHT_RPT_ID_FEATURE) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (len < sizeof(ambient_light_feature_rpt_t)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    {
        fbl::AutoLock lock(&feature_lock_);
        ambient_light_feature_rpt_t* out = static_cast<ambient_light_feature_rpt_t*>(data);
        feature_rpt_ = *out; // TA doesn't work on a memcpy taking an address as in &feature_rpt_
    }

    zx_port_packet packet = {TCS_CONFIGURE, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = zx_port_queue(port_handle_, &packet);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::HidBusSetReport: zx_port_queue failed: %d\n", status);
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

zx_status_t Tcs3400Device::HidBusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidBusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}

zx_status_t Tcs3400Device::Bind() {
    if (device_get_protocol(parent(), ZX_PROTOCOL_I2C, &i2c_) != ZX_OK) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status;
    if (device_get_protocol(parent_, ZX_PROTOCOL_GPIO, &gpio_) != ZX_OK) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    gpio_config(&gpio_, TCS3400_INTERRUPT_IDX, GPIO_DIR_IN);

    status = gpio_get_interrupt(&gpio_, TCS3400_INTERRUPT_IDX,
                                ZX_INTERRUPT_MODE_EDGE_LOW,
                                irq_.reset_and_get_address());
    if (status != ZX_OK) {
        return status;
    }

    status = zx_port_create(1 /*PORT_BIND_TO_INTERRUPT*/, &port_handle_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::Bind: port_create failed: %d\n", status);
        return status;
    }

    status = zx_interrupt_bind(irq_.get(), port_handle_, TCS_INTERRUPT, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::Bind: zx_interrupt_bind failed: %d\n", status);
        return status;
    }

    auto cleanup = fbl::MakeAutoCall([&]() { ShutDown(); });

    {
        fbl::AutoLock lock(&feature_lock_);
        // The device will trigger an interrupt outside the thresholds.  These default threshold
        // values effectively disable interrupts since we can't be outside this range, interrupts
        // get effectively enabled when we configure a range that could trigger.
        feature_rpt_.threshold_low = 0x0000;
        feature_rpt_.threshold_high = 0xFFFF;
        feature_rpt_.interval_ms = 0;
        feature_rpt_.state = HID_USAGE_SENSOR_STATE_INITIALIZING_VAL;
    }

    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<Tcs3400Device*>(arg)->Thread();
                                   },
                                   reinterpret_cast<void*>(this),
                                   "tcs3400-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    status = DdkAdd("tcs-3400");
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::Bind: DdkAdd failed: %d\n", status);
        return status;
    }

    zx_port_packet packet = {TCS_CONFIGURE, ZX_PKT_TYPE_USER, ZX_OK, {}};
    status = zx_port_queue(port_handle_, &packet);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Tcs3400Device::Bind: zx_port_queue failed: %d\n", status);
    }

    cleanup.cancel();
    return ZX_OK;
}

void Tcs3400Device::ShutDown() {
    zx_port_packet packet = {TCS_SHUTDOWN, ZX_PKT_TYPE_USER, ZX_OK, {}};
    zx_status_t status = zx_port_queue(port_handle_, &packet);
    ZX_ASSERT(status == ZX_OK);
    thrd_join(thread_, NULL);
    irq_.destroy();
    {
        fbl::AutoLock lock(&proxy_input_lock_);
        proxy_.clear();
    }
}

void Tcs3400Device::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

void Tcs3400Device::DdkRelease() {
    delete this;
}

} // namespace tcs

extern "C" zx_status_t tcs3400_bind(void* ctx, zx_device_t* parent) {
    auto dev = fbl::make_unique<tcs::Tcs3400Device>(parent);
    auto status = dev->Bind();
    if (status == ZX_OK) {
        // devmgr is now in charge of the memory for dev
        __UNUSED auto ptr = dev.release();
    }
    return status;
}
