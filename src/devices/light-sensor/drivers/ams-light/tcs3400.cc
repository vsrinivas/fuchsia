// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tcs3400.h"

#include <lib/device-protocol/i2c.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <algorithm>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <ddktl/metadata/light-sensor.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hid/descriptor.h>

#include "tcs3400-regs.h"

namespace {
constexpr zx_duration_t INTERRUPTS_HYSTERESIS = ZX_MSEC(100);
constexpr uint8_t SAMPLES_TO_TRIGGER = 0x01;

#define GET_BYTE(val, shift) static_cast<uint8_t>((val >> shift) & 0xFF)

// TODO(37765): -Waddress-of-packed-member warns on pointers to members of
// packed structs because those pointers could be misaligned. The warning
// however can appear on areas where we just copy the value of the pointer
// instead of access it. These macros silence it by casting to a void* and back.
#define UNALIGNED_U16_PTR(val) (uint16_t*)((void*)val)

// clang-format off
// zx_port_packet::type
#define TCS_SHUTDOWN  0x01
#define TCS_CONFIGURE 0x02
#define TCS_INTERRUPT 0x03
#define TCS_REARM_IRQ 0x04
#define TCS_POLL      0x05
// clang-format on

enum {
  FRAGMENT_I2C,
  FRAGMENT_GPIO,
  FRAGMENT_COUNT,
};

}  // namespace

namespace tcs {

zx_status_t Tcs3400Device::FillInputRpt() {
  input_rpt_.rpt_id = AMBIENT_LIGHT_RPT_ID_INPUT;
  struct Regs {
    uint16_t* out;
    uint8_t reg_h;
    uint8_t reg_l;
  } regs[] = {
      {UNALIGNED_U16_PTR(&input_rpt_.illuminance), TCS_I2C_CDATAH, TCS_I2C_CDATAL},
      {UNALIGNED_U16_PTR(&input_rpt_.red), TCS_I2C_RDATAH, TCS_I2C_RDATAL},
      {UNALIGNED_U16_PTR(&input_rpt_.green), TCS_I2C_GDATAH, TCS_I2C_GDATAL},
      {UNALIGNED_U16_PTR(&input_rpt_.blue), TCS_I2C_BDATAH, TCS_I2C_BDATAL},
  };
  for (const auto& i : regs) {
    uint8_t buf_h, buf_l;
    zx_status_t status;
    fbl::AutoLock lock(&i2c_lock_);
    // Read lower byte first, the device holds upper byte of a sample in a shadow register after
    // a lower byte read
    status = i2c_.WriteReadSync(&i.reg_l, 1, &buf_l, 1);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Tcs3400Device::FillInputRpt: i2c_write_read_sync failed: %d", status);
      input_rpt_.state = HID_USAGE_SENSOR_STATE_ERROR_VAL;
      return status;
    }
    status = i2c_.WriteReadSync(&i.reg_h, 1, &buf_h, 1);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Tcs3400Device::FillInputRpt: i2c_write_read_sync failed: %d", status);
      input_rpt_.state = HID_USAGE_SENSOR_STATE_ERROR_VAL;
      return status;
    }
    auto out = static_cast<uint16_t>(static_cast<float>(((buf_h & 0xFF) << 8) | (buf_l & 0xFF)));

    // Use memcpy here because i.out is a misaligned pointer and dereferencing a
    // misaligned pointer is UB. This ends up getting lowered to a 16-bit store.
    memcpy(i.out, &out, sizeof(out));

    zxlogf(DEBUG, "raw: 0x%04X  again: %u  atime: %u", out, again_, atime_);
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
    zx_time_t timeout = std::min(poll_timeout, irq_rearm_timeout);
    zx_status_t status = port_.wait(zx::time(timeout), &packet);
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "Tcs3400Device::Thread: port wait failed: %d", status);
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
        zxlogf(INFO, "Tcs3400Device::Thread: shutting down");
        return thrd_success;
      case TCS_CONFIGURE: {
        fbl::AutoLock lock(&feature_lock_);
        threshold_low = feature_rpt_.threshold_low;
        threshold_high = feature_rpt_.threshold_high;
        if (feature_rpt_.interval_ms == 0) {  // per spec 0 is device's default
          poll_timeout = ZX_TIME_INFINITE;    // we define the default as no polling
        } else {
          poll_timeout = zx_deadline_after(ZX_MSEC(feature_rpt_.interval_ms));
        }
      }
        {
          struct Setup {
            uint8_t cmd;
            uint8_t val;
          } __PACKED setup[] = {
              {TCS_I2C_ENABLE,
               TCS_I2C_ENABLE_POWER_ON | TCS_I2C_ENABLE_ADC_ENABLE | TCS_I2C_ENABLE_INT_ENABLE},
              {TCS_I2C_AILTL, GET_BYTE(threshold_low, 0)},
              {TCS_I2C_AILTH, GET_BYTE(threshold_low, 8)},
              {TCS_I2C_AIHTL, GET_BYTE(threshold_high, 0)},
              {TCS_I2C_AIHTH, GET_BYTE(threshold_high, 8)},
              {TCS_I2C_PERS, SAMPLES_TO_TRIGGER},
          };
          for (const auto& i : setup) {
            fbl::AutoLock lock(&i2c_lock_);
            status = i2c_.WriteSync(&i.cmd, sizeof(setup[0]));
            if (status != ZX_OK) {
              zxlogf(ERROR, "Tcs3400Device::Thread: i2c_write_sync failed: %d", status);
              break;  // do not exit thread, future transactions may succeed
            }
          }
        }
        break;
      case TCS_INTERRUPT:
        zx_interrupt_ack(irq_.get());  // rearm interrupt at the IRQ level
        {
          fbl::AutoLock lock(&feature_lock_);
          threshold_low = feature_rpt_.threshold_low;
          threshold_high = feature_rpt_.threshold_high;
        }
        {
          fbl::AutoLock lock(&client_input_lock_);
          if (FillInputRpt() == ZX_OK && client_.is_valid()) {
            if (input_rpt_.illuminance > threshold_high) {
              input_rpt_.event = HID_USAGE_SENSOR_EVENT_HIGH_THRESHOLD_CROSS_UPWARD_VAL;
              client_.IoQueue(&input_rpt_, sizeof(ambient_light_input_rpt_t),
                              zx_clock_get_monotonic());
            } else if (input_rpt_.illuminance < threshold_low) {
              input_rpt_.event = HID_USAGE_SENSOR_EVENT_LOW_THRESHOLD_CROSS_DOWNWARD_VAL;
              client_.IoQueue(&input_rpt_, sizeof(ambient_light_input_rpt_t),
                              zx_clock_get_monotonic());
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
          status = i2c_.WriteSync(cmd, sizeof(cmd));
          if (status != ZX_OK) {
            zxlogf(ERROR, "Tcs3400Device::Thread: i2c_write_sync failed: %d", status);
            // Continue on error, future transactions may succeed
          }
          irq_rearm_timeout = ZX_TIME_INFINITE;
        }
        break;
      case TCS_POLL: {
        fbl::AutoLock lock(&client_input_lock_);
        if (client_.is_valid()) {
          FillInputRpt();  // We ioqueue even if report filling failed reporting bad state
          input_rpt_.event = HID_USAGE_SENSOR_EVENT_PERIOD_EXCEEDED_VAL;
          client_.IoQueue(&input_rpt_, sizeof(ambient_light_input_rpt_t), zx_clock_get_monotonic());
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

zx_status_t Tcs3400Device::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&client_input_lock_);
  if (client_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  } else {
    client_ = ddk::HidbusIfcProtocolClient(ifc);
  }
  return ZX_OK;
}

zx_status_t Tcs3400Device::HidbusQuery(uint32_t options, hid_info_t* info) {
  if (!info) {
    return ZX_ERR_INVALID_ARGS;
  }
  info->dev_num = 0;
  info->device_class = HID_DEVICE_CLASS_OTHER;
  info->boot_device = false;

  return ZX_OK;
}

void Tcs3400Device::HidbusStop() {}

zx_status_t Tcs3400Device::HidbusGetDescriptor(hid_description_type_t desc_type,
                                               void* out_data_buffer, size_t data_size,
                                               size_t* out_data_actual) {
  const uint8_t* desc;
  size_t desc_size = get_ambient_light_report_desc(&desc);

  if (data_size < desc_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(out_data_buffer, desc, desc_size);
  *out_data_actual = desc_size;
  return ZX_OK;
}

zx_status_t Tcs3400Device::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                           size_t* out_len) {
  if (rpt_id != AMBIENT_LIGHT_RPT_ID_INPUT && rpt_id != AMBIENT_LIGHT_RPT_ID_FEATURE) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *out_len = (rpt_id == AMBIENT_LIGHT_RPT_ID_INPUT) ? sizeof(ambient_light_input_rpt_t)
                                                    : sizeof(ambient_light_feature_rpt_t);
  if (*out_len > len) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (rpt_id == AMBIENT_LIGHT_RPT_ID_INPUT) {
    fbl::AutoLock lock(&client_input_lock_);
    FillInputRpt();
    auto out = static_cast<ambient_light_input_rpt_t*>(data);
    *out = input_rpt_;  // TA doesn't work on a memcpy taking an address as in &input_rpt_
  } else {
    fbl::AutoLock lock(&feature_lock_);
    auto out = static_cast<ambient_light_feature_rpt_t*>(data);
    *out = feature_rpt_;  // TA doesn't work on a memcpy taking an address as in &feature_rpt_
  }
  return ZX_OK;
}

zx_status_t Tcs3400Device::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                           size_t len) {
  if (rpt_id != AMBIENT_LIGHT_RPT_ID_FEATURE) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (len < sizeof(ambient_light_feature_rpt_t)) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  {
    fbl::AutoLock lock(&feature_lock_);
    auto* out = static_cast<const ambient_light_feature_rpt_t*>(data);
    feature_rpt_ = *out;  // TA doesn't work on a memcpy taking an address as in &feature_rpt_
  }

  zx_port_packet packet = {TCS_CONFIGURE, ZX_PKT_TYPE_USER, ZX_OK, {}};
  zx_status_t status = port_.queue(&packet);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Tcs3400Device::HidbusSetReport: zx_port_queue failed: %d", status);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t Tcs3400Device::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Tcs3400Device::HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Tcs3400Device::HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

// static
zx_status_t Tcs3400Device::Create(void* ctx, zx_device_t* parent) {
  composite_protocol_t composite;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
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

  i2c_protocol_t i2c = {};
  if (device_get_protocol(fragments[FRAGMENT_I2C], ZX_PROTOCOL_I2C, &i2c) != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  gpio_protocol_t gpio = {};
  if (device_get_protocol(fragments[FRAGMENT_GPIO], ZX_PROTOCOL_GPIO, &gpio) != ZX_OK) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx::port port;
  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port_create failed: %d", __FILE__, status);
    return status;
  }

  ddk::I2cChannel channel(&i2c);
  auto dev =
      std::make_unique<tcs::Tcs3400Device>(parent, std::move(channel), gpio, std::move(port));
  status = dev->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s bind failed: %d", __FILE__, status);
    return status;
  }

  status = dev->DdkAdd("tcs-3400");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed: %d", __FILE__, status);
    return status;
  }

  // devmgr is now in charge of the memory for dev
  __UNUSED auto ptr = dev.release();
  return status;
}

zx_status_t Tcs3400Device::InitGain(uint8_t gain) {
  if (!(gain == 1 || gain == 4 || gain == 16 || gain == 64)) {
    zxlogf(WARNING, "%s Invalid gain (%u) using gain = 1", __FILE__, gain);
    gain = 1;
  }

  again_ = gain;
  zxlogf(DEBUG, "again (%u)", again_);

  uint8_t reg;
  // clang-format off
  if (gain == 1)  reg = 0;
  if (gain == 4)  reg = 1;
  if (gain == 16) reg = 2;
  if (gain == 64) reg = 3;
  // clang-format on

  const uint8_t command[2] = {TCS_I2C_CONTROL, reg};
  fbl::AutoLock lock(&i2c_lock_);
  auto status = i2c_.WriteSync(command, countof(command));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Setting gain failed %d", __FILE__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Tcs3400Device::InitMetadata() {
  metadata::LightSensorParams parameters = {};
  size_t actual = {};
  auto status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &parameters,
                                    sizeof(metadata::LightSensorParams), &actual);
  if (status != ZX_OK || sizeof(metadata::LightSensorParams) != actual) {
    zxlogf(ERROR, "%s Getting metadata failed %d", __FILE__, status);
    return status;
  }

  // ATIME = 256 - Integration Time / 2.4 ms.
  if (parameters.integration_time_ms <= 615) {
    atime_ = static_cast<uint8_t>(256 - (parameters.integration_time_ms * 10 / 24));
  } else {
    atime_ = 1;
    zxlogf(WARNING, "%s Invalid integration time (%u) using atime = %u", __FILE__,
           parameters.integration_time_ms, atime_);
  }

  zxlogf(DEBUG, "atime (%u)", atime_);
  {
    fbl::AutoLock lock(&i2c_lock_);
    const uint8_t command[2] = {TCS_I2C_ATIME, atime_};
    status = i2c_.WriteSync(command, countof(command));
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s Setting integration time failed %d", __FILE__, status);
      return status;
    }
  }

  return InitGain(parameters.gain);
}

zx_status_t Tcs3400Device::Bind() {
  gpio_config_in(&gpio_, GPIO_NO_PULL);

  auto status =
      gpio_get_interrupt(&gpio_, ZX_INTERRUPT_MODE_EDGE_LOW, irq_.reset_and_get_address());
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s gpio_get_interrupt failed: %d", __FILE__, status);
    return status;
  }

  status = irq_.bind(port_, TCS_INTERRUPT, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_interrupt_bind failed: %d", __FILE__, status);
    return status;
  }

  status = InitMetadata();
  if (status != ZX_OK) {
    return status;
  }

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
  zx_port_packet packet = {TCS_CONFIGURE, ZX_PKT_TYPE_USER, ZX_OK, {}};
  status = port_.queue(&packet);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_port_queue failed: %d", __FILE__, status);
    return status;
  }

  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Tcs3400Device*>(arg)->Thread(); },
      reinterpret_cast<void*>(this), "tcs3400-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

void Tcs3400Device::ShutDown() {
  zx_port_packet packet = {TCS_SHUTDOWN, ZX_PKT_TYPE_USER, ZX_OK, {}};
  zx_status_t status = port_.queue(&packet);
  ZX_ASSERT(status == ZX_OK);
  if (thread_) {
    thrd_join(thread_, NULL);
  }
  irq_.destroy();
  {
    fbl::AutoLock lock(&client_input_lock_);
    client_.clear();
  }
}

void Tcs3400Device::DdkUnbindNew(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}

void Tcs3400Device::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Tcs3400Device::Create;
  return ops;
}();

}  // namespace tcs

// clang-format off
ZIRCON_DRIVER_BEGIN(tcs3400_light, tcs::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMS),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMS_TCS3400),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMS_LIGHT),
ZIRCON_DRIVER_END(tcs3400_light)
    // clang-format on
