// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-hid.h"

#include <endian.h>
#include <lib/zx/time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/hw/i2c.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/trace/event.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

namespace i2c_hid {

// Poll interval: 10 ms
#define I2C_POLL_INTERVAL_USEC 10000

// Send the device a HOST initiated RESET.  Caller must call
// i2c_wait_for_ready_locked() afterwards to guarantee completion.
// If |force| is false, do not issue a reset if there is one outstanding.
zx_status_t I2cHidbus::Reset(bool force) {
  uint16_t cmd_reg = letoh16(hiddesc_.wCommandRegister);
  uint8_t buf[4] = {static_cast<uint8_t>(cmd_reg & 0xff), static_cast<uint8_t>(cmd_reg >> 8), 0x00,
                    0x01};
  fbl::AutoLock lock(&i2c_lock_);

  if (!force && i2c_pending_reset_) {
    return ZX_OK;
  }

  i2c_pending_reset_ = true;
  zx_status_t status = i2c_.WriteSync(buf, sizeof(buf));

  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-hid: could not issue reset: %d\n", status);
    return status;
  }

  return ZX_OK;
}

// Must be called with i2c_lock held.
void I2cHidbus::WaitForReadyLocked() {
  while (i2c_pending_reset_) {
    i2c_reset_cnd_.Wait(&i2c_lock_);
  }
}

zx_status_t I2cHidbus::HidbusQuery(uint32_t options, hid_info_t* info) {
  if (!info) {
    return ZX_ERR_INVALID_ARGS;
  }
  info->dev_num = 0;
  info->device_class = HID_DEVICE_CLASS_OTHER;
  info->boot_device = false;

  info->vendor_id = hiddesc_.wVendorID;
  info->product_id = hiddesc_.wProductID;
  info->version = hiddesc_.wVersionID;
  return ZX_OK;
}

zx_status_t I2cHidbus::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&ifc_lock_);
  if (ifc_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  ifc_ = ddk::HidbusIfcProtocolClient(ifc);
  return ZX_OK;
}

void I2cHidbus::HidbusStop() {
  fbl::AutoLock lock(&ifc_lock_);
  ifc_.clear();
}

zx_status_t I2cHidbus::HidbusGetDescriptor(hid_description_type_t desc_type, void* out_data_buffer,
                                           size_t data_size, size_t* out_data_actual) {
  if (desc_type != HID_DESCRIPTION_TYPE_REPORT) {
    return ZX_ERR_NOT_FOUND;
  }

  fbl::AutoLock lock(&i2c_lock_);
  WaitForReadyLocked();

  size_t desc_len = letoh16(hiddesc_.wReportDescLength);
  uint16_t desc_reg = letoh16(hiddesc_.wReportDescRegister);
  uint16_t buf = htole16(desc_reg);

  if (data_size < desc_len) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  zx_status_t status = i2c_.WriteReadSync(reinterpret_cast<uint8_t*>(&buf), sizeof(uint16_t),
                                          static_cast<uint8_t*>(out_data_buffer), desc_len);
  if (status < 0) {
    zxlogf(ERROR, "i2c-hid: could not read HID report descriptor from reg 0x%04x: %d\n", desc_reg,
           status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  *out_data_actual = desc_len;
  return ZX_OK;
}

// TODO(teisenbe/tkilbourn): Remove this once we pipe IRQs from ACPI
int I2cHidbus::WorkerThreadNoIrq() {
  zxlogf(INFO, "i2c-hid: using noirq\n");

  zx_status_t status = Reset(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-hid: failed to reset i2c device\n");
    return 0;
  }

  uint16_t len = letoh16(hiddesc_.wMaxInputLength);
  uint8_t* buf = static_cast<uint8_t*>(malloc(len));

  // Last report received, so we can deduplicate.  This is only necessary since
  // we haven't wired through interrupts yet, and some devices always return
  // the last received report when you attempt to read from them.
  uint8_t* last_report = static_cast<uint8_t*>(malloc(len));
  size_t last_report_len = 0;

  zx_time_t last_timeout_warning = 0;
  const zx_duration_t kMinTimeBetweenWarnings = ZX_SEC(10);

  // Until we have a way to map the GPIO associated with an i2c slave to an
  // IRQ, we just poll.
  while (!stop_worker_thread_) {
    usleep(I2C_POLL_INTERVAL_USEC);
    TRACE_DURATION("input", "Device Read");

    uint16_t report_len = 0;
    {
      fbl::AutoLock lock(&i2c_lock_);

      // Perform a read with no register address.
      status = i2c_.WriteReadSync(nullptr, 0, buf, len);
      if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
          zx_time_t now = zx_clock_get_monotonic();
          if (now - last_timeout_warning > kMinTimeBetweenWarnings) {
            zxlogf(TRACE, "i2c-hid: device_read timed out\n");
            last_timeout_warning = now;
          }
          continue;
        }
        zxlogf(ERROR, "i2c-hid: device_read failure %d\n", status);
        continue;
      }

      report_len = letoh16(*(uint16_t*)buf);
      if (report_len == 0x0) {
        zxlogf(INFO, "i2c-hid reset detected\n");
        // Either host or device reset.
        i2c_pending_reset_ = false;
        i2c_reset_cnd_.Broadcast();
        continue;
      }

      if (i2c_pending_reset_) {
        zxlogf(INFO, "i2c-hid: received event while waiting for reset? %u\n", report_len);
        continue;
      }
    }

    if ((report_len == 0xffff) || (report_len == 0x3fff)) {
      // nothing to read
      continue;
    }
    if ((report_len < 2) || (report_len > len)) {
      zxlogf(ERROR, "i2c-hid: bad report len (rlen %hu, bytes read %d)!!!\n", report_len, len);
      continue;
    }

    // Check for duplicates.  See comment by |last_report| definition.
    if (last_report_len == report_len && !memcmp(buf, last_report, report_len)) {
      continue;
    }

    {
      fbl::AutoLock lock(&ifc_lock_);
      if (ifc_.is_valid()) {
        ifc_.IoQueue(buf + 2, report_len - 2);
      }
    }

    last_report_len = report_len;

    // Swap buffers
    uint8_t* tmp = last_report;
    last_report = buf;
    buf = tmp;
  }

  free(buf);
  free(last_report);
  return 0;
}

int I2cHidbus::WorkerThreadIrq() {
  zxlogf(TRACE, "i2c-hid: using irq\n");

  zx_status_t status = Reset(true);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-hid: failed to reset i2c device\n");
    return 0;
  }

  uint16_t len = letoh16(hiddesc_.wMaxInputLength);
  uint8_t* buf = static_cast<uint8_t*>(malloc(len));

  zx_time_t last_timeout_warning = 0;
  const zx_duration_t kMinTimeBetweenWarnings = ZX_SEC(10);

  while (true) {
    zx_status_t status = irq_.wait(nullptr);
    if (status != ZX_OK) {
      if (status != ZX_ERR_CANCELED) {
        zxlogf(ERROR, "i2c-hid: interrupt wait failed %d\n", status);
      }
      break;
    }
    if (stop_worker_thread_) {
      break;
    }

    TRACE_DURATION("input", "Device Read");
    uint16_t report_len = 0;
    {
      fbl::AutoLock lock(&i2c_lock_);

      // Perform a read with no register address.
      status = i2c_.WriteReadSync(nullptr, 0, buf, len);
      if (status != ZX_OK) {
        if (status == ZX_ERR_TIMED_OUT) {
          zx_time_t now = zx_clock_get_monotonic();
          if (now - last_timeout_warning > kMinTimeBetweenWarnings) {
            zxlogf(TRACE, "i2c-hid: device_read timed out\n");
            last_timeout_warning = now;
          }
          continue;
        }
        zxlogf(ERROR, "i2c-hid: device_read failure %d\n", status);
        continue;
      }

      report_len = letoh16(*(uint16_t*)buf);
      if (report_len == 0x0) {
        zxlogf(INFO, "i2c-hid reset detected\n");
        // Either host or device reset.
        i2c_pending_reset_ = false;
        i2c_reset_cnd_.Broadcast();
        continue;
      }

      if (i2c_pending_reset_) {
        zxlogf(INFO, "i2c-hid: received event while waiting for reset? %u\n", report_len);
        continue;
      }

      if ((report_len < 2) || (report_len > len)) {
        zxlogf(ERROR, "i2c-hid: bad report len (report_len %hu, bytes_read %d)!!!\n", report_len,
               len);
        continue;
      }
    }

    {
      fbl::AutoLock lock(&ifc_lock_);
      if (ifc_.is_valid()) {
        ifc_.IoQueue(buf + 2, report_len - 2);
      }
    }
  }

  free(buf);
  return 0;
}

void I2cHidbus::Shutdown() {
  stop_worker_thread_ = true;
  if (irq_.is_valid()) {
    irq_.destroy();
  }
  thrd_join(worker_thread_, NULL);

  {
    fbl::AutoLock lock(&ifc_lock_);
    ifc_.clear();
  }
}

void I2cHidbus::DdkUnbindDeprecated() {
  Shutdown();
  DdkRemoveDeprecated();
}

void I2cHidbus::DdkRelease() { delete this; }

zx_status_t I2cHidbus::ReadI2cHidDesc(I2cHidDesc* hiddesc) {
  // TODO: get the address out of ACPI
  uint8_t buf[2];
  uint8_t* data = buf;
  *data++ = 0x01;
  *data++ = 0x00;
  uint8_t out[4];
  zx_status_t status;

  fbl::AutoLock lock(&i2c_lock_);

  status = i2c_.WriteReadSync(buf, sizeof(buf), out, sizeof(out));
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-hid: could not read HID descriptor: %d\n", status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // We can safely cast here because the descriptor length is the first
  // 2 bytes of out.
  uint16_t desc_len = letoh16(*(reinterpret_cast<uint16_t *>(out)));
  if (desc_len > sizeof(I2cHidDesc)) {
    desc_len = sizeof(I2cHidDesc);
  }

  status = i2c_.WriteReadSync(buf, sizeof(buf), reinterpret_cast<uint8_t*>(hiddesc), desc_len);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-hid: could not read HID descriptor: %d\n", status);
    return ZX_ERR_NOT_SUPPORTED;
  }

  zxlogf(TRACE, "i2c-hid: desc:\n");
  zxlogf(TRACE, "  report desc len: %u\n", letoh16(hiddesc->wReportDescLength));
  zxlogf(TRACE, "  report desc reg: %u\n", letoh16(hiddesc->wReportDescRegister));
  zxlogf(TRACE, "  input reg:       %u\n", letoh16(hiddesc->wInputRegister));
  zxlogf(TRACE, "  max input len:   %u\n", letoh16(hiddesc->wMaxInputLength));
  zxlogf(TRACE, "  output reg:      %u\n", letoh16(hiddesc->wOutputRegister));
  zxlogf(TRACE, "  max output len:  %u\n", letoh16(hiddesc->wMaxOutputLength));
  zxlogf(TRACE, "  command reg:     %u\n", letoh16(hiddesc->wCommandRegister));
  zxlogf(TRACE, "  data reg:        %u\n", letoh16(hiddesc->wDataRegister));
  zxlogf(TRACE, "  vendor id:       %x\n", hiddesc->wVendorID);
  zxlogf(TRACE, "  product id:      %x\n", hiddesc->wProductID);
  zxlogf(TRACE, "  version id:      %x\n", hiddesc->wVersionID);

  return ZX_OK;
}

zx_status_t I2cHidbus::Bind(ddk::I2cChannel i2c) {
  zx_status_t status;

  {
    fbl::AutoLock lock(&i2c_lock_);
    i2c_ = std::move(i2c);
    i2c_.GetInterrupt(0, &irq_);
  }

  auto worker_thread = [](void* arg) -> int {
    auto dev = reinterpret_cast<I2cHidbus*>(arg);
    zx_status_t status = ZX_OK;
    // Retry the first transaction a few times; in some cases (e.g. on Slate) the device was powered
    // on explicitly during enumeration, and there is a warmup period after powering on the device
    // during which the device is not responsive over i2c.
    // TODO(jfsulliv): It may make more sense to introduce a delay after powering on the device,
    // rather than here while attempting to bind.
    int retries = 3;
    while (retries-- > 0) {
      if ((status = dev->ReadI2cHidDesc(&dev->hiddesc_)) == ZX_OK) {
        break;
      }
      zx::nanosleep(zx::deadline_after(zx::msec(100)));
      zxlogf(INFO, "i2c-hid: Retrying reading HID descriptor\n");
    }
    if (status != ZX_OK) {
      dev->DdkRemoveDeprecated();
      return thrd_error;
    }
    dev->DdkMakeVisible();

    if (dev->irq_.is_valid()) {
      dev->WorkerThreadIrq();
    } else {
      dev->WorkerThreadNoIrq();
    }
    // If |stop_worker_thread_| is not set, than we exited the worker thread because
    // of an error and not a shutdown. Call DdkRemove directly.
    if (!dev->stop_worker_thread_) {
      dev->DdkRemoveDeprecated();
      return thrd_error;
    }
    return thrd_success;
  };

  status = DdkAdd("i2c-hid", DEVICE_ADD_INVISIBLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "i2c-hid: could not add device: %d\n", status);
    return status;
  }

  int rc = thrd_create_with_name(&worker_thread_, worker_thread, this, "i2c-hid-worker-thread");
  if (rc != thrd_success) {
    DdkRemoveDeprecated();
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

static zx_status_t i2c_hid_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  auto dev = std::make_unique<I2cHidbus>(parent);

  ddk::I2cChannel i2c(parent);
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "I2c-Hid: Could not get i2c protocol\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  status = dev->Bind(std::move(i2c));
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t i2c_hid_driver_ops = []() {
  zx_driver_ops_t i2c_hid_driver_ops = {};
  i2c_hid_driver_ops.version = DRIVER_OPS_VERSION;
  i2c_hid_driver_ops.bind = i2c_hid_bind;
  return i2c_hid_driver_ops;
}();

}  // namespace i2c_hid

// clang-format off
ZIRCON_DRIVER_BEGIN(i2c_hid, i2c_hid::i2c_hid_driver_ops, "zircon", "0.1", 2)
  BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
  BI_MATCH_IF(EQ, BIND_I2C_CLASS, I2C_CLASS_HID),
ZIRCON_DRIVER_END(i2c_hid)
    // clang-format on
