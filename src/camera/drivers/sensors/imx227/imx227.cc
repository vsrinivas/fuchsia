// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx227.h"

#include <endian.h>
#include <lib/device-protocol/i2c.h>
#include <lib/driver-unit-test/utils.h>
#include <lib/fit/result.h>
#include <lib/trace/event.h>
#include <threads.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <ddk/protocol/camera/sensor.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>

#include "src/camera/drivers/sensors/imx227/constants.h"
#include "src/camera/drivers/sensors/imx227/imx227_modes.h"
#include "src/camera/drivers/sensors/imx227/imx227_seq.h"
#include "src/camera/drivers/sensors/imx227/mipi_ccs_regs.h"

namespace camera {

// Gets the register value from the sequence table.
// |id| : Index of the sequence table.
// |address| : Address of the register.
fit::result<uint8_t, zx_status_t> Imx227Device::GetRegisterValueFromSequence(uint8_t index,
                                                                             uint16_t address) {
  TRACE_DURATION("camera", "Imx227Device::GetRegisterValueFromSequence");
  if (index >= kSEQUENCE_TABLE.size()) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  const InitSeqFmt* sequence = kSEQUENCE_TABLE[index];
  while (true) {
    uint16_t register_address = sequence->address;
    uint16_t register_value = sequence->value;
    uint16_t register_len = sequence->len;
    if (register_address == kEndOfSequence && register_value == 0 && register_len == 0) {
      break;
    }
    if (address == register_address) {
      return fit::ok(register_value);
    }
    sequence++;
  }
  return fit::error(ZX_ERR_NOT_FOUND);
}

zx_status_t Imx227Device::InitPdev() {
  std::lock_guard guard(lock_);

  // I2c for communicating with the sensor.
  if (!i2c_.is_valid()) {
    zxlogf(ERROR, "%s; I2C not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  // Clk for gating clocks for sensor.
  if (!clk24_.is_valid()) {
    zxlogf(ERROR, "%s; clk24_ not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  // Mipi for init and de-init.
  if (!mipi_.is_valid()) {
    zxlogf(ERROR, "%s; mipi_ not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  // GPIOs
  if (!gpio_vana_enable_.is_valid()) {
    zxlogf(ERROR, "%s; gpio_vana_enable_ not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  if (!gpio_vdig_enable_.is_valid()) {
    zxlogf(ERROR, "%s; gpio_vdig_enable_ not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  if (!gpio_cam_rst_.is_valid()) {
    zxlogf(ERROR, "%s; gpio_cam_rst_ not available", __func__);
    return ZX_ERR_NO_RESOURCES;
  }

  // Set the GPIO to output and set them to their initial values
  // before the power up sequence.
  gpio_cam_rst_.ConfigOut(1);
  gpio_vana_enable_.ConfigOut(0);
  gpio_vdig_enable_.ConfigOut(0);
  return ZX_OK;
}

fit::result<uint16_t, zx_status_t> Imx227Device::Read16(uint16_t addr) {
  TRACE_DURATION("camera", "Imx227Device::Read16", "addr", addr);
  auto result = Read8(addr);
  if (result.is_error()) {
    return result.take_error_result();
  }
  auto upper_byte = result.value();
  result = Read8(addr + 1);
  if (result.is_error()) {
    return result.take_error_result();
  }
  auto lower_byte = result.value();
  uint16_t reg_value = upper_byte << kByteShift | lower_byte;
  return fit::ok(reg_value);
}

fit::result<uint8_t, zx_status_t> Imx227Device::Read8(uint16_t addr) {
  TRACE_DURATION("camera", "Imx227Device::Read8", "addr", addr);
  // Convert the address to Big Endian format.
  // The camera sensor expects in this format.
  uint16_t buf = htobe16(addr);
  uint8_t val = 0;
  auto status =
      i2c_.WriteReadSync(reinterpret_cast<uint8_t*>(&buf), sizeof(buf), &val, sizeof(val));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Imx227Device: could not read reg addr: 0x%08x  status: %d", addr, status);
    return fit::error(status);
  }
  return fit::ok(val);
}

zx_status_t Imx227Device::Write16(uint16_t addr, uint16_t val) {
  TRACE_DURATION("camera", "Imx227Device::Write16", "addr", addr, "val", val);
  // Convert the arguments to big endian to match the register spec.
  // First two bytes are the address, third and fourth are the value to be written.
  addr = htobe16(addr);
  val = htobe16(val);
  std::array<uint8_t, 4> buf;
  buf[0] = static_cast<uint8_t>(addr & kByteMask);
  buf[1] = static_cast<uint8_t>((addr >> kByteShift) & kByteMask);
  buf[2] = static_cast<uint8_t>(val & kByteMask);
  buf[3] = static_cast<uint8_t>((val >> kByteShift) & kByteMask);
  auto status = i2c_.WriteSync(buf.data(), buf.size());
  if (status != ZX_OK) {
    zxlogf(ERROR,
           "Imx227Device: could not write reg addr/val: 0x%08x/0x%08x status: "
           "%d\n",
           addr, val, status);
  }
  return status;
}

zx_status_t Imx227Device::Write8(uint16_t addr, uint8_t val) {
  TRACE_DURATION("camera", "Imx227Device::Write8", "addr", addr, "val", val);
  // Convert the arguments to big endian to match the register spec.
  // First two bytes are the address, third one is the value to be written.
  addr = htobe16(addr);
  std::array<uint8_t, 3> buf;
  buf[0] = static_cast<uint8_t>(addr & kByteMask);
  buf[1] = static_cast<uint8_t>((addr >> kByteShift) & kByteMask);
  buf[2] = val;

  auto status = i2c_.WriteSync(buf.data(), buf.size());
  if (status != ZX_OK) {
    zxlogf(ERROR,
           "Imx227Device: could not write reg addr/val: 0x%08x/0x%08x status: "
           "%d\n",
           addr, val, status);
  }
  return status;
}

bool Imx227Device::ValidateSensorID() {
  auto result = Read16(kSensorModelIdReg);
  if (result.is_error()) {
    return false;
  }
  return result.value() == kSensorId;
}

zx_status_t Imx227Device::InitSensor(uint8_t idx) {
  TRACE_DURATION("camera", "Imx227Device::InitSensor");
  if (idx >= kSEQUENCE_TABLE.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  const InitSeqFmt* sequence = kSEQUENCE_TABLE[idx];
  bool init_command = true;

  while (init_command) {
    uint16_t address = sequence->address;
    uint8_t value = sequence->value;

    switch (address) {
      case 0x0000: {
        if (sequence->value == 0 && sequence->len == 0) {
          init_command = false;
        } else {
          Write8(address, value);
        }
        break;
      }
      default:
        Write8(address, value);
        break;
    }
    sequence++;
  }
  return ZX_OK;
}

void Imx227Device::HwInit() {
  TRACE_DURATION("camera", "Imx227Device::HwInit");
  // Power up sequence. Reference: Page 51- IMX227-0AQH5-C datasheet.
  gpio_vana_enable_.Write(1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

  gpio_vdig_enable_.Write(1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

  // Enable 24M clock for sensor.
  clk24_.Enable();
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

  gpio_cam_rst_.Write(0);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
}

void Imx227Device::HwDeInit() {
  TRACE_DURATION("camera", "Imx227Device::HwDeInit");
  gpio_cam_rst_.Write(1);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

  clk24_.Disable();
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

  gpio_vdig_enable_.Write(0);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));

  gpio_vana_enable_.Write(0);
  zx_nanosleep(zx_deadline_after(ZX_MSEC(50)));
}

zx_status_t Imx227Device::InitMipiCsi(uint8_t mode) {
  mipi_info_t mipi_info;
  mipi_adap_info_t adap_info;

  mipi_info.lanes = available_modes[mode].lanes;
  mipi_info.ui_value = 1000 / available_modes[mode].mbps;
  if ((1000 % available_modes[mode].mbps) != 0) {
    mipi_info.ui_value += 1;
  }

  adap_info.format = MIPI_IMAGE_FORMAT_AM_RAW10;
  adap_info.resolution.width = available_modes[mode].resolution_in.x;
  adap_info.resolution.height = available_modes[mode].resolution_in.y;
  adap_info.path = MIPI_PATH_PATH0;
  adap_info.mode = MIPI_MODES_DIR_MODE;
  auto status = mipi_.Init(&mipi_info, &adap_info);
  return status;
}

fit::result<int32_t, zx_status_t> Imx227Device::GetTemperature() {
  std::lock_guard guard(lock_);
  // Enable temperature control
  zx_status_t status = Write8(kTempCtrlReg, 0x01);
  if (status != ZX_OK) {
    return fit::error(status);
  }
  auto result = Read8(kTempOutputReg);
  if (result.is_error()) {
    return result.take_error_result();
  }
  auto retval = static_cast<int32_t>(result.value());
  return fit::ok(retval);
}

fit::result<uint32_t, zx_status_t> Imx227Device::GetLinesPerSecond() {
  auto result_hi =
      GetRegisterValueFromSequence(available_modes[current_mode_].idx, kLineLengthPckReg);
  auto result_lo =
      GetRegisterValueFromSequence(available_modes[current_mode_].idx, kLineLengthPckReg + 1);
  if (result_hi.is_error() || result_lo.is_error()) {
    return fit::error(ZX_ERR_INTERNAL);
  }
  uint16_t line_length_pclk = (result_hi.value() << 8) | result_lo.value();
  uint32_t lines_per_second = kMasterClock / line_length_pclk;
  return fit::ok(lines_per_second);
}

float Imx227Device::AnalogRegValueToTotalGain(uint16_t reg_value) {
  return (static_cast<float>(analog_gain_.m0_ * reg_value + analog_gain_.c0_)) /
         (static_cast<float>(analog_gain_.m1_ * reg_value + analog_gain_.c1_));
}

uint16_t Imx227Device::AnalogTotalGainToRegValue(float gain) {
  float value;
  uint16_t register_value;

  // Compute the register value.
  if (analog_gain_.m0_ == 0) {
    value = ((analog_gain_.c0_ / gain) - analog_gain_.c1_) / analog_gain_.m1_;
  } else {
    value = (analog_gain_.c1_ * gain - analog_gain_.c0_) / analog_gain_.m0_;
  }

  // Round the final result, which is quantized to the gain code step size.
  value += 0.5f * analog_gain_.gain_code_step_size_;

  // Convert and clamp.
  register_value = value;

  if (register_value < analog_gain_.gain_code_min_) {
    register_value = analog_gain_.gain_code_min_;
  }

  register_value =
      (register_value - analog_gain_.gain_code_min_) / analog_gain_.gain_code_step_size_;
  register_value = register_value * analog_gain_.gain_code_step_size_ + analog_gain_.gain_code_min_;

  if (register_value > analog_gain_.gain_code_max_) {
    register_value = analog_gain_.gain_code_max_;
  }

  return register_value;
}

float Imx227Device::DigitalRegValueToTotalGain(uint16_t reg_value) {
  return static_cast<float>(reg_value) / (1 << kDigitalGainShift);
}

uint16_t Imx227Device::DigitalTotalGainToRegValue(float gain) {
  float value;
  uint16_t register_value;

  // Compute the register value.
  value = gain * (1 << kDigitalGainShift);

  // Round the final result, which is quantized to the gain code step size.
  value += 0.5f * digital_gain_.gain_step_size_;

  // Convert and clamp.
  register_value = value;

  if (register_value < digital_gain_.gain_min_) {
    register_value = digital_gain_.gain_min_;
  }

  register_value = (register_value - digital_gain_.gain_min_) / digital_gain_.gain_step_size_;
  register_value = register_value * digital_gain_.gain_step_size_ + digital_gain_.gain_min_;

  if (register_value > digital_gain_.gain_max_) {
    register_value = digital_gain_.gain_max_;
  }

  return register_value;
}
zx_status_t Imx227Device::ReadAnalogGainConstants() {
  auto result_m0 = Read16(kAnalogGainM0Reg);
  auto result_m1 = Read16(kAnalogGainM1Reg);
  auto result_c0 = Read16(kAnalogGainC0Reg);
  auto result_c1 = Read16(kAnalogGainC1Reg);
  auto result_amin = Read16(kAnalogGainCodeMinReg);
  auto result_amax = Read16(kAnalogGainCodeMaxReg);
  auto result_astep = Read16(kAnalogGainCodeStepSizeReg);

  if (result_m0.is_error() || result_m1.is_error() || result_c0.is_error() ||
      result_c1.is_error() || result_amin.is_error() || result_amax.is_error() ||
      result_astep.is_error()) {
    return ZX_ERR_BAD_STATE;
  }

  analog_gain_.m0_ = result_m0.value();
  analog_gain_.m1_ = result_m1.value();
  analog_gain_.c0_ = result_c0.value();
  analog_gain_.c1_ = result_c1.value();
  analog_gain_.gain_code_min_ = result_amin.value();
  analog_gain_.gain_code_max_ = result_amax.value();
  analog_gain_.gain_code_step_size_ = result_astep.value();

  // Validate the m0,1 constraint
  if (!(analog_gain_.m0_ == 0) ^ (analog_gain_.m1_ == 0)) {
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

zx_status_t Imx227Device::ReadDigitalGainConstants() {
  auto result_dmin = Read16(kDigitalGainMinReg);
  auto result_dmax = Read16(kDigitalGainMaxReg);
  auto result_dstep = Read16(kDigitalGainStepSizeReg);

  if (result_dmin.is_error() || result_dmax.is_error() || result_dstep.is_error()) {
    return ZX_ERR_BAD_STATE;
  }

  digital_gain_.gain_min_ = result_dmin.value();
  digital_gain_.gain_max_ = result_dmax.value();
  digital_gain_.gain_step_size_ = result_dstep.value();
  return ZX_OK;
}

// TODO(jsasinowski): Determine if this can be called less frequently.
zx_status_t Imx227Device::ReadGainConstants() {
  if (gain_constants_valid_) {
    return ZX_OK;
  }

  auto status = ReadAnalogGainConstants();
  if (status != ZX_OK) {
    return status;
  }

  status = ReadDigitalGainConstants();
  if (status != ZX_OK) {
    return status;
  }

  gain_constants_valid_ = true;
  return ZX_OK;
}

zx_status_t Imx227Device::SetGroupedParameterHold(bool enable) {
  auto status = Write8(kGroupedParameterHoldReg, enable ? 1 : 0);
  return status;
}

zx_status_t Imx227Device::Create(zx_device_t* parent, std::unique_ptr<Imx227Device>* device_out) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "%s could not get composite protocoln", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  std::array<zx_device_t*, FRAGMENT_COUNT> fragments;
  size_t actual;
  composite.GetFragments(fragments.data(), FRAGMENT_COUNT, &actual);
  if (actual != FRAGMENT_COUNT) {
    zxlogf(ERROR, "%s Could not get fragments", __func__);
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto sensor_device = std::make_unique<Imx227Device>(
      parent, fragments[FRAGMENT_I2C], fragments[FRAGMENT_GPIO_VANA], fragments[FRAGMENT_GPIO_VDIG],
      fragments[FRAGMENT_GPIO_CAM_RST], fragments[FRAGMENT_CLK24], fragments[FRAGMENT_MIPICSI]);

  zx_status_t status = sensor_device->InitPdev();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s InitPdev failed", __func__);
    return status;
  }
  *device_out = std::move(sensor_device);
  return status;
}

void Imx227Device::ShutDown() {}

void Imx227Device::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void Imx227Device::DdkRelease() {
  ShutDown();
  delete this;
}

zx_status_t Imx227Device::CreateAndBind(void* /*ctx*/, zx_device_t* parent) {
  std::unique_ptr<Imx227Device> device;
  zx_status_t status = Imx227Device::Create(parent, &device);
  if (status != ZX_OK) {
    zxlogf(ERROR, "imx227: Could not setup imx227 sensor device: %d", status);
    return status;
  }
  std::array<zx_device_prop_t, 1> props = {{
      {BIND_PLATFORM_PROTO, 0, ZX_PROTOCOL_CAMERA_SENSOR2},
  }};

  status = device->DdkAdd(
      ddk::DeviceAddArgs("imx227").set_flags(DEVICE_ADD_ALLOW_MULTI_COMPOSITE).set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "imx227: Could not add imx227 sensor device: %d", status);
    return status;
  }
  zxlogf(INFO, "imx227 driver added");

  // `device` intentionally leaked as it is now held by DevMgr.
  __UNUSED auto* dev = device.release();
  return ZX_OK;
}

bool Imx227Device::RunUnitTests(void* ctx, zx_device_t* parent, zx_handle_t channel) {
  return driver_unit_test::RunZxTests("Imx227Tests", parent, channel);
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Imx227Device::CreateAndBind;
  ops.run_unit_tests = Imx227Device::RunUnitTests;
  return ops;
}();

}  // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(imx227, camera::driver_ops, "imx227", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SONY),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_SONY_IMX227),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_CAMERA_SENSOR),
ZIRCON_DRIVER_END(imx227)
