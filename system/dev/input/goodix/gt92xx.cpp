// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include "gt92xx.h"

namespace goodix {
// Configuration data
// first two bytes contain starting register address (part of i2c transaction)
static uint8_t conf_data[] = {
    GT_REG_CONFIG_DATA >> 8, GT_REG_CONFIG_DATA & 0xff,
    0x5C, 0x00, 0x04, 0x58, 0x02, 0x05, 0xBD, 0xC0,
    0x00, 0x08, 0x1E, 0x05, 0x50, 0x32, 0x05, 0x0B,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x12, 0x00, 0x17,
    0x17, 0x19, 0x12, 0x8D, 0x2D, 0x0F, 0x3F, 0x41,
    0xB2, 0x04, 0x00, 0x00, 0x00, 0xBC, 0x03, 0x1D,
    0x1E, 0x80, 0x01, 0x00, 0x14, 0x46, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x37, 0x55, 0x8F, 0xC5, 0x02,
    0x07, 0x11, 0x00, 0x04, 0x8A, 0x39, 0x00, 0x81,
    0x3E, 0x00, 0x78, 0x44, 0x00, 0x71, 0x4A, 0x00,
    0x6A, 0x51, 0x00, 0x6A, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x1C, 0x1A, 0x18, 0x16, 0x14, 0x12, 0x10, 0x0E,
    0x0C, 0x0A, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00,
    0xFF, 0xFF, 0x1F, 0xE7, 0xFF, 0xFF, 0xFF, 0x0F,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x2A, 0x29,
    0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
    0x20, 0x1F, 0x1E, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x6C, 0x01};

int Gt92xxDevice::Thread() {
    zx_status_t status;
    zxlogf(INFO, "gt92xx: entering irq thread\n");
    while (true) {
        status = irq_.wait(nullptr);
        if (!running_.load()) {
            return ZX_OK;
        }
        if (status != ZX_OK) {
            zxlogf(ERROR, "gt92xx: Interrupt error %d\n", status);
        }
        uint8_t touch_stat = Read(GT_REG_TOUCH_STATUS);
        if (touch_stat & 0x80) {
            uint8_t num_reports = touch_stat & 0x0f;
            FingerReport reports[kMaxPoints];
            // Read touch reports
            zx_status_t status =
                Read(GT_REG_REPORTS, reinterpret_cast<uint8_t*>(&reports),
                     static_cast<uint8_t>(sizeof(FingerReport) * kMaxPoints));
            if (status == ZX_OK) {
                fbl::AutoLock lock(&proxy_lock_);
                gt_rpt_.rpt_id = GT92XX_RPT_ID_TOUCH;
                gt_rpt_.contact_count = num_reports;
                // We are reusing same HID report as ft3x77 to simplify astro integration
                // so we need to copy from device format to HID structure format
                for (uint32_t i = 0; i < kMaxPoints; i++) {
                    gt_rpt_.fingers[i].finger_id = static_cast<uint8_t>((reports[i].id << 2) |
                        ((i < num_reports) ? 1 : 0));
                    gt_rpt_.fingers[i].y = reports[i].x;
                    gt_rpt_.fingers[i].x = reports[i].y;
                }
                if (proxy_.is_valid()) {
                    proxy_.IoQueue(reinterpret_cast<uint8_t*>(&gt_rpt_), sizeof(gt92xx_touch_t));
                }
            }
            // Clear the touch status
            Write(GT_REG_TOUCH_STATUS, 0);
        }
    }
    zxlogf(INFO, "gt92xx: exiting\n");
    return 0;
}

zx_status_t Gt92xxDevice::Create(zx_device_t* device) {

    zxlogf(INFO, "gt92xx: driver started...\n");

    pdev_protocol_t pdev_proto;
    zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_PDEV, &pdev_proto);
    if (status) {
        zxlogf(ERROR, "%s could not acquire platform device\n", __func__);
        return status;
    }
    ddk::PDev pdev(&pdev_proto);


    auto i2c = pdev.GetI2c(0);
    auto intr = pdev.GetGpio(0);
    auto reset = pdev.GetGpio(1);
    if (!i2c || !intr || !reset) {
        zxlogf(ERROR, "%s failed to allocate gpio or i2c\n", __func__);
        return ZX_ERR_NO_RESOURCES;
    }
    auto goodix_dev = fbl::make_unique<Gt92xxDevice>(device,
                                                     fbl::move(*i2c),
                                                     fbl::move(*intr),
                                                     fbl::move(*reset));

    status = goodix_dev->Init();
    if (status != ZX_OK) {
        zxlogf(ERROR, "Could not initialize gt92xx hardware %d\n", status);
        return status;
    }

    auto thunk = [](void* arg) -> int {
        return reinterpret_cast<Gt92xxDevice*>(arg)->Thread();
    };

    auto cleanup = fbl::MakeAutoCall([&]() { goodix_dev->ShutDown(); });

    goodix_dev->running_.store(true);
    int ret = thrd_create_with_name(&goodix_dev->thread_, thunk,
                                    goodix_dev.get(),
                                    "gt92xx-thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);

    status = goodix_dev->DdkAdd("gt92xx HidDevice\n");
    if (status != ZX_OK) {
        zxlogf(ERROR, "gt92xx: Could not create hid device: %d\n", status);
        return status;
    } else {
        zxlogf(INFO, "gt92xx: Added hid device\n");
    }

    cleanup.cancel();

    // device intentionally leaked as it is now held by DevMgr
    __UNUSED auto ptr = goodix_dev.release();

    return ZX_OK;
}

zx_status_t Gt92xxDevice::Init() {
    // Hardware reset
    HWReset();

    uint8_t fw = Read(GT_REG_FIRMWARE);
    if (fw != GT_FIRMWARE_MAGIC) {
        zxlogf(ERROR, "Invalid gt92xx firmware configuration!\n");
        return ZX_ERR_BAD_STATE;
    }
    // Device requires 50ms delay after this check (per datasheet)
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

    // Configuration data should span specific set of registers
    // last register has flag to latch in new configuration, second
    // to last register holds checksum of register values.
    // Note: first two bytes of conf_data hold the 16-bit register address where
    // the write will start.
    ZX_DEBUG_ASSERT((countof(conf_data) - sizeof(uint16_t)) ==
                    (GT_REG_CONFIG_REFRESH - GT_REG_CONFIG_DATA + 1));

    // Write conf data to registers
    zx_status_t status = i2c_.WriteReadSync(conf_data, sizeof(conf_data), NULL, 0);
    if (status != ZX_OK) {
        return status;
    }
    // Device requires 10ms delay to refresh configuration
    zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
    // Clear touch state in case there were spurious touches registered
    // during startup
    Write(GT_REG_TOUCH_STATUS, 0);

    status = int_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_HIGH, irq_.reset_and_get_address());

    return status;
}

void Gt92xxDevice::HWReset() {
    // Hardware reset will also set the address of the controller to either
    // 0x14 0r 0x5d.  See the datasheet for explanation of sequence.
    reset_gpio_.ConfigOut(0); //Make reset pin an output and pull low
    int_gpio_.ConfigOut(0);   //Make interrupt pin an output and pull low

    // Delay for 100us
    zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

    reset_gpio_.Write(1); // Release the reset
    zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
    int_gpio_.ConfigIn(0);                        // Make interrupt pin an input again;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(50))); // Wait for reset to complete
}

zx_status_t Gt92xxDevice::HidbusQuery(uint32_t options, hid_info_t* info) {
    if (!info) {
        return ZX_ERR_INVALID_ARGS;
    }
    info->dev_num = 0;
    info->device_class = HID_DEVICE_CLASS_OTHER;
    info->boot_device = false;

    return ZX_OK;
}

void Gt92xxDevice::DdkRelease() {
    delete this;
}

void Gt92xxDevice::DdkUnbind() {
    ShutDown();
    DdkRemove();
}

zx_status_t Gt92xxDevice::ShutDown() {
    running_.store(false);
    irq_.destroy();
    thrd_join(thread_, NULL);
    {
        fbl::AutoLock lock(&proxy_lock_);
        proxy_.clear();
    }
    return ZX_OK;
}

zx_status_t Gt92xxDevice::HidbusGetDescriptor(uint8_t desc_type, void** data, size_t* len) {

    const uint8_t* desc_ptr;
    uint8_t* buf;
    *len = get_gt92xx_report_desc(&desc_ptr);
    fbl::AllocChecker ac;
    buf = new (&ac) uint8_t[*len];
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    memcpy(buf, desc_ptr, *len);
    *data = buf;
    return ZX_OK;
}

zx_status_t Gt92xxDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                          size_t len, size_t* out_len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Gt92xxDevice::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                          size_t len) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Gt92xxDevice::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Gt92xxDevice::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Gt92xxDevice::HidbusGetProtocol(uint8_t* protocol) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Gt92xxDevice::HidbusSetProtocol(uint8_t protocol) {
    return ZX_OK;
}

void Gt92xxDevice::HidbusStop() {
    fbl::AutoLock lock(&proxy_lock_);
    proxy_.clear();
}

zx_status_t Gt92xxDevice::HidbusStart(const hidbus_ifc_t* ifc) {
    fbl::AutoLock lock(&proxy_lock_);
    if (proxy_.is_valid()) {
        zxlogf(ERROR, "gt92xx: Already bound!\n");
        return ZX_ERR_ALREADY_BOUND;
    } else {
        proxy_ = ddk::HidbusIfcProxy(ifc);
        zxlogf(INFO, "gt92xx: started\n");
    }
    return ZX_OK;
}

uint8_t Gt92xxDevice::Read(uint16_t addr) {
    uint8_t rbuf;
    Read(addr, &rbuf, 1);
    return rbuf;
}

zx_status_t Gt92xxDevice::Read(uint16_t addr, uint8_t* buf, uint8_t len) {
    uint8_t tbuf[2];
    tbuf[0] = static_cast<uint8_t>(addr >> 8);
    tbuf[1] = static_cast<uint8_t>(addr & 0xff);
    return i2c_.WriteReadSync(tbuf, 2, buf, len);
}

zx_status_t Gt92xxDevice::Write(uint16_t addr, uint8_t val) {
    uint8_t tbuf[3];
    tbuf[0] = static_cast<uint8_t>(addr >> 8);
    tbuf[1] = static_cast<uint8_t>(addr & 0xff);
    tbuf[2] = val;
    return i2c_.WriteReadSync(tbuf, 3, NULL, 0);
}

} // namespace ft

__BEGIN_CDECLS

zx_status_t gt92xx_bind(void* ctx, zx_device_t* device) {
    return goodix::Gt92xxDevice::Create(device);
}

static zx_driver_ops_t gt92xx_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = gt92xx_bind,
    .create = nullptr,
    .release = nullptr,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(gt92xx, gt92xx_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_ASTRO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ASTRO_GOODIXTOUCH),
ZIRCON_DRIVER_END(gt92xx)
// clang-format on
__END_CDECLS
