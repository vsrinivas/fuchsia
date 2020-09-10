// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gt92xx.h"

#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <iterator>
#include <utility>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddk/trace/event.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>

namespace goodix {
enum {
  FRAGMENT_I2C,
  FRAGMENT_INT_GPIO,
  FRAGMENT_RESET_GPIO,
  FRAGMENT_COUNT,
};

// clang-format off
// Configuration data
// first two bytes contain starting register address (part of i2c transaction)
fbl::Vector<uint8_t> Gt92xxDevice::GetConfData() {
  return {GT_REG_CONFIG_DATA >> 8,
              GT_REG_CONFIG_DATA & 0xff,
              0x5f, 0x00, 0x04, 0x58, 0x02, 0x05, 0xbd, 0xc0,
              0x00, 0x08, 0x1e, 0x05, 0x50, 0x32, 0x00, 0x0b,
              0x00, 0x00, 0x00, 0x00, 0x40, 0x12, 0x00, 0x17,
              0x17, 0x19, 0x12, 0x8d, 0x2d, 0x0f, 0x3f, 0x41,
              0xb2, 0x04, 0x00, 0x00, 0x00, 0xbc, 0x03, 0x1d,
              0x1e, 0x80, 0x01, 0x00, 0x14, 0x46, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x37, 0x55, 0x8f, 0xc5, 0x02,
              0x07, 0x11, 0x00, 0x04, 0x8a, 0x39, 0x00, 0x81,
              0x3e, 0x00, 0x78, 0x44, 0x00, 0x71, 0x4a, 0x00,
              0x6a, 0x51, 0x00, 0x6a, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
              0x1c, 0x1a, 0x18, 0x16, 0x14, 0x12, 0x10, 0x0e,
              0x0c, 0x0a, 0x08, 0x06, 0x04, 0x02, 0x00, 0x00,
              0xff, 0xff, 0x1f, 0xe7, 0xff, 0xff, 0xff, 0x0f,
              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x2a, 0x29,
              0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21,
              0x20, 0x1f, 0x1e, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
              0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x6e, 0x01 };
}
// clang-format on

int Gt92xxDevice::Thread() {
  zx_status_t status;
  zx::time timestamp;
  zxlogf(INFO, "gt92xx: entering irq thread");
  while (true) {
    status = irq_.wait(&timestamp);
    if (!running_.load()) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "gt92xx: Interrupt error %d", status);
    }
    TRACE_DURATION("input", "Gt92xxDevice Read");
    uint8_t touch_stat = 0;
    uint8_t retry_cnt = 0;
    // Datasheet implies that it is not guaranteed that report will be
    // ready when interrupt is generated, so allow a couple retries to check
    // touch status.
    while (!(touch_stat & GT_REG_TOUCH_STATUS_READY) && (retry_cnt < 3)) {
      touch_stat = Read(GT_REG_TOUCH_STATUS);
      if (!(touch_stat & GT_REG_TOUCH_STATUS_READY)) {
        retry_cnt++;
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
      }
    }

    if (touch_stat & GT_REG_TOUCH_STATUS_READY) {
      uint8_t num_reports = touch_stat & 0x0f;
      FingerReport reports[kMaxPoints];
      // Read touch reports
      zx_status_t status = Read(GT_REG_REPORTS, reinterpret_cast<uint8_t*>(&reports),
                                static_cast<uint8_t>(sizeof(FingerReport) * kMaxPoints));
      // Clear touch status after reading reports
      Write(GT_REG_TOUCH_STATUS, 0);
      if (status == ZX_OK) {
        fbl::AutoLock lock(&client_lock_);
        gt_rpt_.rpt_id = GT92XX_RPT_ID_TOUCH;
        gt_rpt_.contact_count = num_reports;
        // We are reusing same HID report as ft3x77 to simplify astro integration
        // so we need to copy from device format to HID structure format
        for (uint32_t i = 0; i < kMaxPoints; i++) {
          gt_rpt_.fingers[i].finger_id =
              static_cast<uint8_t>((reports[i].id << 2) | ((i < num_reports) ? 1 : 0));
          gt_rpt_.fingers[i].y = reports[i].x;
          gt_rpt_.fingers[i].x = reports[i].y;
        }
        if (client_.is_valid()) {
          client_.IoQueue(reinterpret_cast<uint8_t*>(&gt_rpt_), sizeof(gt92xx_touch_t),
                          timestamp.get());
        }
      }
    } else {
      zxlogf(ERROR, "gt92xx: Errant interrupt, no report ready - %x", touch_stat);
    }
  }
  zxlogf(INFO, "gt92xx: exiting");
  return 0;
}

zx_status_t Gt92xxDevice::Create(zx_device_t* device) {
  zxlogf(INFO, "gt92xx: driver started...");

  composite_protocol_t composite;
  auto status = device_get_protocol(device, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol");
    return status;
  }

  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite_get_fragments(&composite, fragments, std::size(fragments), &actual);
  if (actual != std::size(fragments)) {
    zxlogf(ERROR, "could not get fragments");
    return ZX_ERR_NOT_SUPPORTED;
  }

  i2c_protocol_t i2c;
  status = device_get_protocol(fragments[FRAGMENT_I2C], ZX_PROTOCOL_I2C, &i2c);
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: failed to acquire i2c");
    return status;
  }

  gpio_protocol_t int_gpio;
  status = device_get_protocol(fragments[FRAGMENT_INT_GPIO], ZX_PROTOCOL_GPIO, &int_gpio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: failed to acquire gpio");
    return status;
  }

  gpio_protocol_t reset_gpio;
  status = device_get_protocol(fragments[FRAGMENT_RESET_GPIO], ZX_PROTOCOL_GPIO, &reset_gpio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: failed to acquire gpio");
    return status;
  }

  auto goodix_dev = std::make_unique<Gt92xxDevice>(device, &i2c, &int_gpio, &reset_gpio);

  status = goodix_dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not initialize gt92xx hardware %d", status);
    return status;
  }

  auto thunk = [](void* arg) -> int { return reinterpret_cast<Gt92xxDevice*>(arg)->Thread(); };

  auto cleanup = fbl::MakeAutoCall([&]() { goodix_dev->ShutDown(); });

  goodix_dev->running_.store(true);
  int ret = thrd_create_with_name(&goodix_dev->thread_, thunk, goodix_dev.get(), "gt92xx-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);

  // Set profile for bus transaction thread.
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    const zx::duration capacity = zx::usec(200);
    const zx::duration deadline = zx::msec(1);
    const zx::duration period = deadline;

    zx::profile profile;
    status =
        device_get_deadline_profile(goodix_dev->zxdev(), capacity.get(), deadline.get(),
                                    period.get(), "gt92xx-thread", profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARNING, "Gt92xxDevice::Create: Failed to get deadline profile: %s",
             zx_status_get_string(status));
    } else {
      status = zx_object_set_profile(thrd_get_zx_handle(goodix_dev->thread_), profile.get(), 0);
      if (status != ZX_OK) {
        zxlogf(WARNING,
               "Gt92xxDevice::Create: Failed to apply deadline profile to dispatch thread: %s",
               zx_status_get_string(status));
      }
    }
  }

  status = goodix_dev->DdkAdd("gt92xx HidDevice");
  if (status != ZX_OK) {
    zxlogf(ERROR, "gt92xx: Could not create hid device: %d", status);
    return status;
  } else {
    zxlogf(INFO, "gt92xx: Added hid device");
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
    zxlogf(ERROR, "Invalid gt92xx firmware configuration!");
    return ZX_ERR_BAD_STATE;
  }
  // Device requires 50ms delay after this check (per datasheet)
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

  // Get the config data
  fbl::Vector<uint8_t> Conf(GetConfData());

  // Configuration data should span specific set of registers
  // last register has flag to latch in new configuration, second
  // to last register holds checksum of register values.
  // Note: first two bytes of conf_data hold the 16-bit register address where
  // the write will start.
  ZX_DEBUG_ASSERT((Conf.size() - sizeof(uint16_t)) ==
                  (GT_REG_CONFIG_REFRESH - GT_REG_CONFIG_DATA + 1));

  // Write conf data to registers
  zx_status_t status = i2c_.WriteReadSync(&Conf[0], Conf.size(), NULL, 0);
  if (status != ZX_OK) {
    return status;
  }
  // Device requires 10ms delay to refresh configuration
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
  // Clear touch state in case there were spurious touches registered
  // during startup
  Write(GT_REG_TOUCH_STATUS, 0);

  // Note: Our configuration inverts polarity of interrupt
  // (datasheet implies it is active high)
  status = int_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &irq_);

  return status;
}

void Gt92xxDevice::HWReset() {
  // Hardware reset will also set the address of the controller to either
  // 0x14 0r 0x5d.  See the datasheet for explanation of sequence.
  reset_gpio_.ConfigOut(0);  // Make reset pin an output and pull low
  int_gpio_.ConfigOut(0);    // Make interrupt pin an output and pull low

  // Delay for 100us
  zx_nanosleep(zx_deadline_after(ZX_USEC(100)));

  reset_gpio_.Write(1);  // Release the reset
  zx_nanosleep(zx_deadline_after(ZX_MSEC(5)));
  int_gpio_.ConfigIn(0);                         // Make interrupt pin an input again;
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));  // Wait for reset to complete
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

void Gt92xxDevice::DdkRelease() { delete this; }

void Gt92xxDevice::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

zx_status_t Gt92xxDevice::ShutDown() {
  running_.store(false);
  irq_.destroy();
  thrd_join(thread_, NULL);
  {
    fbl::AutoLock lock(&client_lock_);
    client_.clear();
  }
  return ZX_OK;
}

zx_status_t Gt92xxDevice::HidbusGetDescriptor(hid_description_type_t desc_type,
                                              void* out_data_buffer, size_t data_size,
                                              size_t* out_data_actual) {
  const uint8_t* desc;
  size_t desc_size = get_gt92xx_report_desc(&desc);
  if (data_size < desc_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_data_buffer, desc, desc_size);
  *out_data_actual = desc_size;

  return ZX_OK;
}

zx_status_t Gt92xxDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                          size_t* out_len) {
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

zx_status_t Gt92xxDevice::HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Gt92xxDevice::HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

void Gt92xxDevice::HidbusStop() {
  fbl::AutoLock lock(&client_lock_);
  client_.clear();
}

zx_status_t Gt92xxDevice::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&client_lock_);
  if (client_.is_valid()) {
    zxlogf(ERROR, "gt92xx: Already bound!");
    return ZX_ERR_ALREADY_BOUND;
  } else {
    client_ = ddk::HidbusIfcProtocolClient(ifc);
    zxlogf(INFO, "gt92xx: started");
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

}  // namespace goodix

__BEGIN_CDECLS

zx_status_t gt92xx_bind(void* ctx, zx_device_t* device) {
  return goodix::Gt92xxDevice::Create(device);
}

static constexpr zx_driver_ops_t gt92xx_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = gt92xx_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(gt92xx, gt92xx_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_ASTRO),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_ASTRO_GOODIXTOUCH),
ZIRCON_DRIVER_END(gt92xx)
    // clang-format on
    __END_CDECLS
