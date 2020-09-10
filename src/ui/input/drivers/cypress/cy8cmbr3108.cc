// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cy8cmbr3108.h"

#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <hid/visalia-touch.h>

namespace {
enum {
  kI2cFragment,
  kTouchGpioFragment,
  kFragmentCount,
};
}  // namespace

namespace cypress {

bool Cy8cmbr3108::RunTest(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<Cy8cmbr3108>(new (&ac) Cy8cmbr3108(parent));
  if (!ac.check()) {
    return false;
  }
  auto status = dev->Init();
  if (status != ZX_OK) {
    return false;
  }
  return dev->Test();
}

bool Cy8cmbr3108::Test() {
  auto status_reg = SENSOR_EN::Get().FromValue(0);
  zx_status_t status = RegisterOp(READ, status_reg);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read sensor status :%d", status);
    ShutDown();
    return false;
  }
  zxlogf(INFO, "Sensors enabled : 0x%x", status_reg.reg_value());
  zxlogf(INFO, "Touch the sensors to execute the test..");

  auto button = BUTTON_STAT::Get().FromValue(0);
  for (uint32_t i = 0; i < 100; i++) {
    status = RegisterOp(READ, button);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to get button status :%d", status);
      ShutDown();
      return false;
    }
    zxlogf(INFO, "Button stat register - 0x%x", button.reg_value());
    zx::nanosleep(zx::deadline_after(zx::duration(ZX_MSEC(200))));
  }

  zxlogf(INFO, "Cypress touch test passed");
  ShutDown();
  return true;
}

int Cy8cmbr3108::Thread() {
  while (1) {
    zx_port_packet_t packet;
    zx_status_t status = port_.wait(zx::time::infinite(), &packet);
    zxlogf(DEBUG, "%s msg received on port key %lu", __FUNCTION__, packet.key);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s port wait failed %d", __FUNCTION__, status);
      return thrd_error;
    }

    if (packet.key == kPortKeyShutDown) {
      zxlogf(INFO, "Cy8cmbr3108 thread shutting down");
      return thrd_success;
    }

    visalia_touch_buttons_input_rpt_t input_rpt;
    size_t out_len;
    status = HidbusGetReport(0, BUTTONS_RPT_ID_INPUT, &input_rpt, sizeof(input_rpt), &out_len);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s HidbusGetReport failed %d", __FUNCTION__, status);
    } else {
      fbl::AutoLock lock(&client_lock_);
      if (client_.is_valid()) {
        client_.IoQueue(&input_rpt, sizeof(visalia_touch_buttons_input_rpt_t),
                        zx_clock_get_monotonic());
        // If report could not be filled, we do not ioqueue.
      }
    }
    touch_irq_.ack();
  }  // while (1)

  return thrd_success;
}

template <class DerivedType, class IntType, size_t AddrIntSize, class ByteOrder>
zx_status_t Cy8cmbr3108::RegisterOp(
    uint32_t op, hwreg::I2cRegisterBase<DerivedType, IntType, AddrIntSize, ByteOrder>& reg) {
  // cy8cmbr3108 is known to return NACK while it is busy processing commands or transitioning
  // states. In this case, i2c commands need to be retried until a ACK is received. Typically it
  // goes into deep-sleep state after 340ms of inactivity, so i2c command failures are quite common.

  uint32_t timeout = 0;
  zx_status_t status = ZX_ERR_INTERNAL;

  while (status != ZX_OK) {
    if (op == READ) {
      status = reg.ReadFrom(i2c_);
    } else if (op == WRITE) {
      status = reg.WriteTo(i2c_);
    }
    if (status != ZX_OK) {
      timeout++;
      // i2c transaction is supposed to succeed at least by the end of 3 retries
      if (timeout > 5) {
        return status;
      }
      zx::nanosleep(zx::deadline_after(zx::duration(ZX_USEC(50))));
    }
  }
  return status;
}

zx_status_t Cy8cmbr3108::HidbusStart(const hidbus_ifc_protocol_t* ifc) {
  fbl::AutoLock lock(&client_lock_);
  if (client_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  } else {
    client_ = ddk::HidbusIfcProtocolClient(ifc);
  }
  return ZX_OK;
}

zx_status_t Cy8cmbr3108::HidbusQuery(uint32_t options, hid_info_t* info) {
  if (!info) {
    return ZX_ERR_INVALID_ARGS;
  }
  info->dev_num = 0;
  info->device_class = HID_DEVICE_CLASS_OTHER;
  info->boot_device = false;

  return ZX_OK;
}

void Cy8cmbr3108::HidbusStop() {
  fbl::AutoLock lock(&client_lock_);
  client_.clear();
}

zx_status_t Cy8cmbr3108::HidbusGetDescriptor(hid_description_type_t desc_type,
                                             void* out_data_buffer, size_t data_size,
                                             size_t* out_data_actual) {
  const uint8_t* desc;
  size_t desc_size = get_visalia_touch_buttons_report_desc(&desc);
  if (data_size < desc_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  memcpy(out_data_buffer, desc, desc_size);
  *out_data_actual = desc_size;
  return ZX_OK;
}

zx_status_t Cy8cmbr3108::HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len,
                                         size_t* out_len) {
  if (!data || !out_len) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (rpt_id != BUTTONS_RPT_ID_INPUT) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  *out_len = sizeof(visalia_touch_buttons_input_rpt_t);
  if (*out_len > len) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  visalia_touch_buttons_input_rpt_t input_rpt = {};
  input_rpt.rpt_id = BUTTONS_RPT_ID_INPUT;

  auto button_reg = BUTTON_STAT::Get().FromValue(0);
  auto status = RegisterOp(READ, button_reg);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s Failed to read button register %d", __FUNCTION__, status);
    return status;
  }

  for (size_t i = 0; i < buttons_.size(); ++i) {
    bool new_value = false;  // A value true means a button is touched.
    uint32_t mask = (1 << buttons_[i].idx);
    if (mask & button_reg.reg_value()) {
      new_value = true;
    }

    zxlogf(DEBUG, "%s new value %u for button %lu", __FUNCTION__, new_value, i);
    fill_visalia_touch_buttons_report(buttons_[i].id, new_value, &input_rpt);
  }
  auto out = static_cast<visalia_touch_buttons_input_rpt_t*>(data);
  *out = input_rpt;

  return ZX_OK;
}

zx_status_t Cy8cmbr3108::HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data,
                                         size_t len) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Cy8cmbr3108::HidbusGetIdle(uint8_t rpt_id, uint8_t* duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Cy8cmbr3108::HidbusSetIdle(uint8_t rpt_id, uint8_t duration) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Cy8cmbr3108::HidbusGetProtocol(uint8_t* protocol) { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Cy8cmbr3108::HidbusSetProtocol(uint8_t protocol) { return ZX_OK; }

void Cy8cmbr3108::ShutDown() {
  zx_port_packet packet = {kPortKeyShutDown, ZX_PKT_TYPE_USER, ZX_OK, {}};
  zx_status_t status = port_.queue(&packet);
  ZX_ASSERT(status == ZX_OK);
  thrd_join(thread_, NULL);
  touch_gpio_.ReleaseInterrupt();
  touch_irq_.destroy();
  fbl::AutoLock lock(&client_lock_);
  client_.clear();
}

void Cy8cmbr3108::DdkUnbind(ddk::UnbindTxn txn) {
  ShutDown();
  txn.Reply();
}
void Cy8cmbr3108::DdkRelease() { delete this; }

zx_status_t Cy8cmbr3108::InitializeProtocols() {
  composite_protocol_t composite;
  auto status = device_get_protocol(parent(), ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Get ZX_PROTOCOL_COMPOSITE failed", __func__);
    return status;
  }

  zx_device_t* fragments[kFragmentCount];
  size_t actual;
  composite_get_fragments(&composite, fragments, countof(fragments), &actual);
  if (actual != kFragmentCount) {
    zxlogf(ERROR, "%s: Invalid fragment count (need %d, have %zu)", __func__, kFragmentCount,
           actual);
    return ZX_ERR_INTERNAL;
  }

  // Get I2C and GPIO protocol.
  i2c_ = ddk::I2cProtocolClient(fragments[kI2cFragment]);
  if (!i2c_.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_I2C not found, err=%d", __func__, status);
    return status;
  }

  touch_gpio_ = ddk::GpioProtocolClient(fragments[kTouchGpioFragment]);
  if (!touch_gpio_.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_GPIO not found, err=%d", __func__, status);
    return status;
  }

  // Get buttons metadata.
  actual = 0;
  status = device_get_metadata_size(parent(), DEVICE_METADATA_PRIVATE, &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s device_get_metadata_size failed %d", __FILE__, status);
    return ZX_OK;
  }

  size_t n_buttons = actual / sizeof(touch_button_config_t);
  fbl::AllocChecker ac;
  buttons_ = fbl::Array(new (&ac) touch_button_config_t[n_buttons], n_buttons);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  actual = 0;
  status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, buttons_.get(),
                               buttons_.size() * sizeof(touch_button_config_t), &actual);
  if (status != ZX_OK || actual != buttons_.size() * sizeof(touch_button_config_t)) {
    zxlogf(ERROR, "%s device_get_metadata failed %d", __FILE__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Cy8cmbr3108::Init() {
  // wait upto I2CBOOT time
  zx::nanosleep(zx::deadline_after(zx::duration(ZX_MSEC(15))));

  zx_status_t status = InitializeProtocols();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to initialize protocols %d", __FUNCTION__, status);
    return status;
  }

  /* Note: The default sensor configuration works for visalia and hence not configuring those
   * registers. Add those changes here if needed.*/

  status = touch_gpio_.SetAltFunction(0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s failed to SetAltFunction touch GPIO %d", __FUNCTION__, status);
    return status;
  }

  status = touch_gpio_.ConfigIn(GPIO_NO_PULL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s:failed to ConfigIn touch GPIO %d", __func__, status);
    return status;
  }

  status = touch_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_HIGH, &touch_irq_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: failed to GetInterrupt touch GPIO %d", __func__, status);
    return status;
  }

  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s port_create failed %d", __FUNCTION__, status);
    return status;
  }

  status = touch_irq_.bind(port_, kPortKeyTouchIrq, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_interrupt_bind failed %d", __FUNCTION__, status);
    return status;
  }

  auto f = [](void* arg) -> int { return reinterpret_cast<Cy8cmbr3108*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, f, this, "cypress-irq-thread");
  if (rc != thrd_success) {
    ShutDown();
    return ZX_ERR_INTERNAL;
  }

  return status;
}

zx_status_t Cy8cmbr3108::Bind() {
  zx_status_t status = DdkAdd("cy8cmbr3108");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s DdkAdd failed: %d", __FUNCTION__, status);
  }

  return status;
}

zx_status_t Cy8cmbr3108::Create(void* ctx, zx_device_t* parent) {
  auto dev = std::make_unique<Cy8cmbr3108>(parent);

  zx_status_t status = ZX_OK;
  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  if ((status = dev->Bind()) != ZX_OK) {
    dev->ShutDown();
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = dev.release();
  return ZX_OK;
}

static constexpr zx_driver_ops_t cypress_touch_driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = Cy8cmbr3108::Create;
  driver_ops.run_unit_tests = Cy8cmbr3108::RunTest;
  return driver_ops;
}();

}  // namespace cypress

// clang-format off
ZIRCON_DRIVER_BEGIN(cypress_cy8cmbr3108, cypress::cypress_touch_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_TOUCH),
ZIRCON_DRIVER_END(cypress_cy8cmbr3108)
//clang-format on
