// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ft_device.h"

#include <lib/device-protocol/i2c.h>
#include <lib/focaltech/focaltech.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <hw/arch_ops.h>
#include <hw/reg.h>

namespace ft {

enum {
  COMPONENT_I2C,
  COMPONENT_INT_GPIO,
  COMPONENT_RESET_GPIO,
  COMPONENT_COUNT,
};

FtDevice::FtDevice(zx_device_t* device) : ddk::Device<FtDevice, ddk::UnbindableNew>(device) {}

void FtDevice::ParseReport(ft3x27_finger_t* rpt, uint8_t* buf) {
  rpt->x = static_cast<uint16_t>(((buf[0] & 0x0f) << 8) + buf[1]);
  rpt->y = static_cast<uint16_t>(((buf[2] & 0x0f) << 8) + buf[3]);
  rpt->finger_id = static_cast<uint8_t>(((buf[2] >> 2) & FT3X27_FINGER_ID_CONTACT_MASK) |
                                        (((buf[0] & 0xC0) == 0x80) ? 1 : 0));
}

int FtDevice::Thread() {
  zx_status_t status;
  zxlogf(INFO, "focaltouch: entering irq thread\n");
  while (true) {
    status = irq_.wait(nullptr);
    if (!running_.load()) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "focaltouch: Interrupt error %d\n", status);
    }
    uint8_t i2c_buf[kMaxPoints * kFingerRptSize + 1];
    status = Read(FTS_REG_CURPOINT, i2c_buf, kMaxPoints * kFingerRptSize + 1);
    if (status == ZX_OK) {
      fbl::AutoLock lock(&client_lock_);
      ft_rpt_.rpt_id = FT3X27_RPT_ID_TOUCH;
      ft_rpt_.contact_count = i2c_buf[0];
      for (uint i = 0; i < kMaxPoints; i++) {
        ParseReport(&ft_rpt_.fingers[i], &i2c_buf[i * kFingerRptSize + 1]);
      }
      if (client_.is_valid()) {
        client_.IoQueue(reinterpret_cast<uint8_t*>(&ft_rpt_), sizeof(ft3x27_touch_t));
      }
    } else {
      zxlogf(ERROR, "focaltouch: i2c read error\n");
    }
  }
  zxlogf(INFO, "focaltouch: exiting\n");
}

zx_status_t FtDevice::Init() {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not get composite protocol\n");
    return status;
  }

  zx_device_t* components[COMPONENT_COUNT];
  size_t actual;
  composite_get_components(&composite, components, fbl::count_of(components), &actual);
  if (actual != fbl::count_of(components)) {
    zxlogf(ERROR, "could not get components\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = device_get_protocol(components[COMPONENT_I2C], ZX_PROTOCOL_I2C, &i2c_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: failed to acquire i2c\n");
    return status;
  }

  status =
      device_get_protocol(components[COMPONENT_INT_GPIO], ZX_PROTOCOL_GPIO, &gpios_[FT_INT_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: failed to acquire gpio\n");
    return status;
  }

  status = device_get_protocol(components[COMPONENT_RESET_GPIO], ZX_PROTOCOL_GPIO,
                               &gpios_[FT_RESET_PIN]);
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: failed to acquire gpio\n");
    return status;
  }

  gpio_config_in(&gpios_[FT_INT_PIN], GPIO_NO_PULL);

  status = gpio_get_interrupt(&gpios_[FT_INT_PIN], ZX_INTERRUPT_MODE_EDGE_LOW,
                              irq_.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  uint32_t device_id;
  status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &device_id, sizeof(device_id),
                               &actual);
  if (status != ZX_OK || sizeof(device_id) != actual) {
    zxlogf(ERROR, "focaltouch: failed to read metadata\n");
    return status == ZX_OK ? ZX_ERR_INTERNAL : status;
  }

  if (device_id == FOCALTECH_DEVICE_FT3X27) {
    descriptor_len_ = get_ft3x27_report_desc(&descriptor_);
  } else if (device_id == FOCALTECH_DEVICE_FT6336) {
    descriptor_len_ = get_ft6336_report_desc(&descriptor_);
  } else if (device_id == FOCALTECH_DEVICE_FT5726) {
    descriptor_len_ = get_ft5726_report_desc(&descriptor_);
  } else {
    zxlogf(ERROR, "focaltouch: unknown device ID %u\n", device_id);
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t FtDevice::Create(void* ctx, zx_device_t* device) {
  zxlogf(INFO, "focaltouch: driver started...\n");

  auto ft_dev = std::make_unique<FtDevice>(device);
  zx_status_t status = ft_dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: Driver bind failed %d\n", status);
    return status;
  }

  auto thunk = [](void* arg) -> int { return reinterpret_cast<FtDevice*>(arg)->Thread(); };

  auto cleanup = fbl::MakeAutoCall([&]() { ft_dev->ShutDown(); });

  ft_dev->running_.store(true);
  int ret = thrd_create_with_name(&ft_dev->thread_, thunk, reinterpret_cast<void*>(ft_dev.get()),
                                  "focaltouch-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);

  status = ft_dev->DdkAdd("focaltouch HidDevice");
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: Could not create hid device: %d\n", status);
    return status;
  } else {
    zxlogf(INFO, "focaltouch: Added hid device\n");
  }

  cleanup.cancel();

  // device intentionally leaked as it is now held by DevMgr
  __UNUSED auto ptr = ft_dev.release();

  return ZX_OK;
}

zx_status_t FtDevice::HidbusQuery(uint32_t options, hid_info_t* info) {
  if (!info) {
    return ZX_ERR_INVALID_ARGS;
  }
  info->dev_num = 0;
  info->device_class = HID_DEVICE_CLASS_OTHER;
  info->boot_device = false;

  return ZX_OK;
}

void FtDevice::DdkRelease() { delete this; }

void FtDevice::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

zx_status_t FtDevice::ShutDown() {
  running_.store(false);
  irq_.destroy();
  thrd_join(thread_, NULL);
  {
    fbl::AutoLock lock(&client_lock_);
    // client_.clear();
  }
  return ZX_OK;
}

zx_status_t FtDevice::HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                          size_t data_size, size_t* out_data_actual) {
  if (data_size < descriptor_len_) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(out_data_buffer, descriptor_, descriptor_len_);
  *out_data_actual = descriptor_len_;
  return ZX_OK;
}

zx_status_t FtDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                      size_t* out_len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FtDevice::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                      size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FtDevice::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FtDevice::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FtDevice::HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t FtDevice::HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

void FtDevice::HidbusStop() {
  fbl::AutoLock lock(&client_lock_);
  client_.clear();
}

zx_status_t FtDevice::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&client_lock_);
  if (client_.is_valid()) {
    zxlogf(ERROR, "focaltouch: Already bound!\n");
    return ZX_ERR_ALREADY_BOUND;
  } else {
    client_ = ddk::HidbusIfcProtocolClient(ifc);
    zxlogf(INFO, "focaltouch: started\n");
  }
  return ZX_OK;
}

// simple i2c read for reading one register location
//  intended mostly for debug purposes
uint8_t FtDevice::Read(uint8_t addr) {
  uint8_t rbuf;
  i2c_write_read_sync(&i2c_, &addr, 1, &rbuf, 1);
  return rbuf;
}

zx_status_t FtDevice::Read(uint8_t addr, uint8_t* buf, size_t len) {
  // TODO(bradenkell): Remove this workaround when transfers of more than 8 bytes are supported on
  // the MT8167.
  while (len > 0) {
    size_t readlen = fbl::min(len, kMaxI2cTransferLength);

    zx_status_t status = i2c_write_read_sync(&i2c_, &addr, 1, buf, readlen);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read i2c - %d\n", status);
      return status;
    }

    addr = static_cast<uint8_t>(addr + readlen);
    buf += readlen;
    len -= readlen;
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = FtDevice::Create;
  return ops;
}();

}  // namespace ft

// clang-format off
ZIRCON_DRIVER_BEGIN(focaltech_touch, ft::driver_ops, "focaltech-touch", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_FOCALTOUCH),
ZIRCON_DRIVER_END(focaltech_touch)
    // clang-format on
