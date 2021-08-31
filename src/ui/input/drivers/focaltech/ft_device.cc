// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ft_device.h"

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/hw/arch_ops.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/trace/event.h>
#include <lib/fit/defer.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <algorithm>
#include <iterator>

#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "src/ui/input/drivers/focaltech/focaltech_touch_bind.h"

namespace ft {

FtDevice::FtDevice(zx_device_t* device) : ddk::Device<FtDevice, ddk::Unbindable>(device) {}

void FtDevice::ParseReport(ft3x27_finger_t* rpt, uint8_t* buf) {
  rpt->x = static_cast<uint16_t>(((buf[0] & 0x0f) << 8) + buf[1]);
  rpt->y = static_cast<uint16_t>(((buf[2] & 0x0f) << 8) + buf[3]);
  rpt->finger_id = static_cast<uint8_t>(((buf[2] >> 2) & FT3X27_FINGER_ID_CONTACT_MASK) |
                                        (((buf[0] & 0xC0) == 0x80) ? 1 : 0));
}

int FtDevice::Thread() {
  zx_status_t status;
  zx::time timestamp;
  zxlogf(INFO, "focaltouch: entering irq thread");
  while (true) {
    status = irq_.wait(&timestamp);
    if (!running_.load()) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "focaltouch: Interrupt error %d", status);
    }
    TRACE_DURATION("input", "FtDevice Read");
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
        client_.IoQueue(reinterpret_cast<uint8_t*>(&ft_rpt_), sizeof(ft3x27_touch_t),
                        timestamp.get());
      }
    } else {
      zxlogf(ERROR, "focaltouch: i2c read error");
    }
  }
  zxlogf(INFO, "focaltouch: exiting");
}

zx_status_t FtDevice::Init() {
  i2c_ = ddk::I2cChannel(parent(), "i2c");
  if (!i2c_.is_valid()) {
    zxlogf(ERROR, "failed to acquire i2c");
    return ZX_ERR_NO_RESOURCES;
  }

  int_gpio_ = ddk::GpioProtocolClient(parent(), "gpio-int");
  if (!int_gpio_.is_valid()) {
    zxlogf(ERROR, "failed to acquire int gpio");
    return ZX_ERR_NO_RESOURCES;
  }

  reset_gpio_ = ddk::GpioProtocolClient(parent(), "gpio-reset");
  if (!reset_gpio_.is_valid()) {
    zxlogf(ERROR, "focaltouch: failed to acquire gpio");
    return ZX_ERR_NO_RESOURCES;
  }

  int_gpio_.ConfigIn(GPIO_NO_PULL);

  zx_status_t status = int_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &irq_);
  if (status != ZX_OK) {
    return status;
  }

  size_t actual;
  FocaltechMetadata device_info;
  status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &device_info, sizeof(device_info),
                               &actual);
  if (status != ZX_OK || sizeof(device_info) != actual) {
    zxlogf(ERROR, "focaltouch: failed to read metadata");
    return status == ZX_OK ? ZX_ERR_INTERNAL : status;
  }

  if (device_info.device_id == FOCALTECH_DEVICE_FT3X27) {
    descriptor_len_ = get_ft3x27_report_desc(&descriptor_);
  } else if (device_info.device_id == FOCALTECH_DEVICE_FT6336) {
    descriptor_len_ = get_ft6336_report_desc(&descriptor_);
  } else if (device_info.device_id == FOCALTECH_DEVICE_FT5726) {
    descriptor_len_ = get_ft5726_report_desc(&descriptor_);
  } else {
    zxlogf(ERROR, "focaltouch: unknown device ID %u", device_info.device_id);
    return ZX_ERR_INTERNAL;
  }

  // Reset the chip -- should be low for at least 1ms, and the chip should take at most 200ms to
  // initialize.
  reset_gpio_.ConfigOut(0);
  zx::nanosleep(zx::deadline_after(zx::msec(5)));
  reset_gpio_.Write(1);
  zx::nanosleep(zx::deadline_after(zx::msec(200)));

  status = UpdateFirmwareIfNeeded(device_info);
  if (status != ZX_OK) {
    return status;
  }

  node_ = inspector_.GetRoot().CreateChild("Chip info");
  LogRegisterValue(FTS_REG_TYPE, "TYPE");
  LogRegisterValue(FTS_REG_FIRMID, "FIRMID");
  LogRegisterValue(FTS_REG_VENDOR_ID, "VENDOR_ID");
  LogRegisterValue(FTS_REG_PANEL_ID, "PANEL_ID");
  LogRegisterValue(FTS_REG_RELEASE_ID_HIGH, "RELEASE_ID_HIGH");
  LogRegisterValue(FTS_REG_RELEASE_ID_LOW, "RELEASE_ID_LOW");
  LogRegisterValue(FTS_REG_IC_VERSION, "IC_VERSION");

  if (device_info.needs_firmware) {
    node_.CreateUint("Display vendor", device_info.display_vendor, &values_);
    node_.CreateUint("DDIC version", device_info.ddic_version, &values_);
    zxlogf(INFO, "Display vendor: %u", device_info.display_vendor);
    zxlogf(INFO, "DDIC version:   %u", device_info.ddic_version);
  } else {
    node_.CreateString("Display vendor", "none", &values_);
    node_.CreateString("DDIC version", "none", &values_);
    zxlogf(INFO, "Display vendor: none");
    zxlogf(INFO, "DDIC version:   none");
  }

  return ZX_OK;
}

zx_status_t FtDevice::Create(void* ctx, zx_device_t* device) {
  zxlogf(INFO, "focaltouch: driver started...");

  auto ft_dev = std::make_unique<FtDevice>(device);
  zx_status_t status = ft_dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: Driver bind failed %d", status);
    return status;
  }

  auto thunk = [](void* arg) -> int { return reinterpret_cast<FtDevice*>(arg)->Thread(); };

  auto cleanup = fit::defer([&]() { ft_dev->ShutDown(); });

  ft_dev->running_.store(true);
  int ret = thrd_create_with_name(&ft_dev->thread_, thunk, reinterpret_cast<void*>(ft_dev.get()),
                                  "focaltouch-thread");
  ZX_DEBUG_ASSERT(ret == thrd_success);

  // Set profile for device thread.
  // TODO(fxbug.dev/40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    const zx::duration capacity = zx::usec(200);
    const zx::duration deadline = zx::msec(1);
    const zx::duration period = deadline;

    zx::profile profile;
    status =
        device_get_deadline_profile(ft_dev->zxdev(), capacity.get(), deadline.get(), period.get(),
                                    "focaltouch-thread", profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARNING, "focaltouch: Failed to get deadline profile: %s",
             zx_status_get_string(status));
    } else {
      status = zx_object_set_profile(thrd_get_zx_handle(ft_dev->thread_), profile.get(), 0);
      if (status != ZX_OK) {
        zxlogf(WARNING, "focaltouch: Failed to apply deadline profile to device thread: %s",
               zx_status_get_string(status));
      }
    }
  }

  status = ft_dev->DdkAdd(ddk::DeviceAddArgs("focaltouch HidDevice")
                              .set_inspect_vmo(ft_dev->inspector_.DuplicateVmo()));
  if (status != ZX_OK) {
    zxlogf(ERROR, "focaltouch: Could not create hid device: %d", status);
    return status;
  } else {
    zxlogf(INFO, "focaltouch: Added hid device");
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
  info->vendor_id = static_cast<uint32_t>(fuchsia_input_report::wire::VendorId::kGoogle);
  info->product_id = static_cast<uint32_t>(
      fuchsia_input_report::wire::VendorGoogleProductId::kFocaltechTouchscreen);

  return ZX_OK;
}

void FtDevice::DdkRelease() { delete this; }

void FtDevice::DdkUnbind(ddk::UnbindTxn txn) {
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

zx_status_t FtDevice::HidbusGetDescriptor(hid_description_type_t desc_type,
                                          uint8_t* out_data_buffer, size_t data_size,
                                          size_t* out_data_actual) {
  if (data_size < descriptor_len_) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(out_data_buffer, descriptor_, descriptor_len_);
  *out_data_actual = descriptor_len_;
  return ZX_OK;
}

zx_status_t FtDevice::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, uint8_t* data, size_t len,
                                      size_t* out_len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t FtDevice::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const uint8_t* data,
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
    zxlogf(ERROR, "focaltouch: Already bound!");
    return ZX_ERR_ALREADY_BOUND;
  } else {
    client_ = ddk::HidbusIfcProtocolClient(ifc);
    zxlogf(INFO, "focaltouch: started");
  }
  return ZX_OK;
}

// simple i2c read for reading one register location
//  intended mostly for debug purposes
uint8_t FtDevice::Read(uint8_t addr) {
  uint8_t rbuf;
  i2c_.WriteReadSync(&addr, 1, &rbuf, 1);
  return rbuf;
}

zx_status_t FtDevice::Read(uint8_t addr, uint8_t* buf, size_t len) {
  // TODO(bradenkell): Remove this workaround when transfers of more than 8 bytes are supported on
  // the MT8167.
  while (len > 0) {
    size_t readlen = std::min(len, kMaxI2cTransferLength);

    zx_status_t status = i2c_.WriteReadSync(&addr, 1, buf, readlen);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to read i2c - %d", status);
      return status;
    }

    addr = static_cast<uint8_t>(addr + readlen);
    buf += readlen;
    len -= readlen;
  }

  return ZX_OK;
}

void FtDevice::LogRegisterValue(uint8_t addr, const char* name) {
  uint8_t value;
  zx_status_t status = Read(addr, &value, sizeof(value));
  if (status == ZX_OK) {
    node_.CreateByteVector(name, {value}, &values_);
    zxlogf(INFO, "  %-16s: 0x%02x", name, value);
  } else {
    node_.CreateString(name, "error", &values_);
    zxlogf(ERROR, "  %-16s: error %d", name, status);
  }
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = FtDevice::Create;
  return ops;
}();

}  // namespace ft

ZIRCON_DRIVER(focaltech_touch, ft::driver_ops, "focaltech-touch", "0.1");
