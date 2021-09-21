// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tcs3400.h"

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/i2c.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/clock.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>

#include <ddktl/fidl.h>
#include <ddktl/metadata/light-sensor.h>
#include <fbl/auto_lock.h>

#include "src/devices/light-sensor/drivers/ams-light/tcs3400_light_bind.h"
#include "tcs3400-regs.h"

namespace {
constexpr zx_duration_t INTERRUPTS_HYSTERESIS = ZX_MSEC(100);
constexpr uint8_t SAMPLES_TO_TRIGGER = 0x01;

// Repeat saturated log line every two minutes
constexpr uint16_t kSaturatedLogTimeSecs = 120;
// Bright, not saturated values to return when saturated
constexpr uint16_t kMaxSaturationRed = 21'067;
constexpr uint16_t kMaxSaturationGreen = 20'395;
constexpr uint16_t kMaxSaturationBlue = 20'939;
constexpr uint16_t kMaxSaturationClear = 65'085;

constexpr int64_t kIntegrationTimeStepSizeMicroseconds = 2780;
constexpr int64_t kMinIntegrationTimeStep = 1;
constexpr int64_t kMaxIntegrationTimeStep = 256;

#define GET_BYTE(val, shift) static_cast<uint8_t>(((val) >> (shift)) & 0xFF)

// clang-format off
// zx_port_packet::type
#define TCS_SHUTDOWN  0x01
#define TCS_CONFIGURE 0x02
#define TCS_INTERRUPT 0x03
#define TCS_REARM_IRQ 0x04
#define TCS_POLL      0x05
// clang-format on

constexpr fuchsia_input_report::wire::Axis kLightSensorAxis = {
    .range = {.min = 0, .max = UINT16_MAX},
    .unit =
        {
            .type = fuchsia_input_report::wire::UnitType::kOther,
            .exponent = 0,
        },
};

constexpr fuchsia_input_report::wire::Axis kReportIntervalAxis = {
    .range = {.min = 0, .max = INT64_MAX},
    .unit =
        {
            .type = fuchsia_input_report::wire::UnitType::kSeconds,
            .exponent = -6,
        },
};

constexpr fuchsia_input_report::wire::Axis kSensitivityAxis = {
    .range = {.min = 1, .max = 64},
    .unit =
        {
            .type = fuchsia_input_report::wire::UnitType::kOther,
            .exponent = 0,
        },
};

constexpr fuchsia_input_report::wire::Axis kSamplingRateAxis = {
    .range = {.min = kIntegrationTimeStepSizeMicroseconds,
              .max = kIntegrationTimeStepSizeMicroseconds * kMaxIntegrationTimeStep},
    .unit =
        {
            .type = fuchsia_input_report::wire::UnitType::kSeconds,
            .exponent = -6,
        },
};

constexpr fuchsia_input_report::wire::SensorAxis MakeLightSensorAxis(
    fuchsia_input_report::wire::SensorType type) {
  return {.axis = kLightSensorAxis, .type = type};
}

template <typename T>
bool FeatureValueValid(int64_t value, const T& axis) {
  return value >= axis.range.min && value <= axis.range.max;
}

}  // namespace

namespace tcs {

void Tcs3400InputReport::ToFidlInputReport(fuchsia_input_report::wire::InputReport& input_report,
                                           fidl::AnyArena& allocator) {
  fidl::VectorView<int64_t> values(allocator, 4);
  values[0] = illuminance;
  values[1] = red;
  values[2] = green;
  values[3] = blue;

  const auto sensor_report =
      fuchsia_input_report::wire::SensorInputReport(allocator).set_values(allocator, values);
  input_report.set_event_time(allocator, event_time.get()).set_sensor(allocator, sensor_report);
}

fuchsia_input_report::wire::FeatureReport Tcs3400FeatureReport::ToFidlFeatureReport(
    fidl::AnyArena& allocator) const {
  fidl::VectorView<int64_t> sens(allocator, 1);
  sens[0] = sensitivity;

  fidl::VectorView<int64_t> thresh_high(allocator, 1);
  thresh_high[0] = threshold_high;

  fidl::VectorView<int64_t> thresh_low(allocator, 1);
  thresh_low[0] = threshold_low;

  const auto sensor_report = fuchsia_input_report::wire::SensorFeatureReport(allocator)
                                 .set_report_interval(allocator, report_interval_us)
                                 .set_reporting_state(allocator, reporting_state)
                                 .set_sensitivity(allocator, sens)
                                 .set_threshold_high(allocator, thresh_high)
                                 .set_threshold_low(allocator, thresh_low)
                                 .set_sampling_rate(allocator, integration_time_us);

  return fuchsia_input_report::wire::FeatureReport(allocator).set_sensor(allocator, sensor_report);
}

zx::status<Tcs3400InputReport> Tcs3400Device::ReadInputRpt() {
  Tcs3400InputReport report{.event_time = zx::clock::get_monotonic()};

  bool saturatedReading = false;
  struct Regs {
    int64_t* out;
    uint8_t reg_h;
    uint8_t reg_l;
  } regs[] = {
      {&report.illuminance, TCS_I2C_CDATAH, TCS_I2C_CDATAL},
      {&report.red, TCS_I2C_RDATAH, TCS_I2C_RDATAL},
      {&report.green, TCS_I2C_GDATAH, TCS_I2C_GDATAL},
      {&report.blue, TCS_I2C_BDATAH, TCS_I2C_BDATAL},
  };

  for (const auto& i : regs) {
    uint8_t buf_h, buf_l;
    zx_status_t status;
    // Read lower byte first, the device holds upper byte of a sample in a shadow register after
    // a lower byte read
    status = ReadReg(i.reg_l, buf_l);
    if (status != ZX_OK) {
      zxlogf(ERROR, "i2c_write_read_sync failed: %d", status);
      return zx::error(status);
    }
    status = ReadReg(i.reg_h, buf_h);
    if (status != ZX_OK) {
      zxlogf(ERROR, "i2c_write_read_sync failed: %d", status);
      return zx::error(status);
    }
    auto out = static_cast<uint16_t>(static_cast<float>(((buf_h & 0xFF) << 8) | (buf_l & 0xFF)));

    // Use memcpy here because i.out is a misaligned pointer and dereferencing a
    // misaligned pointer is UB. This ends up getting lowered to a 16-bit store.
    memcpy(i.out, &out, sizeof(out));
    saturatedReading |= (out == 65'535);

    zxlogf(DEBUG, "raw: 0x%04X  again: %u  atime: %u", out, again_, atime_);
  }
  if (saturatedReading) {
    // Saturated, ignoring the IR channel because we only looked at RGBC.
    // Return very bright value so that consumers can adjust screens etc accordingly.
    report.red = kMaxSaturationRed;
    report.green = kMaxSaturationGreen;
    report.blue = kMaxSaturationBlue;
    report.illuminance = kMaxSaturationClear;
    // log one message when saturation starts and then
    if (!isSaturated_ || difftime(time(nullptr), lastSaturatedLog_) >= kSaturatedLogTimeSecs) {
      zxlogf(INFO, "sensor is saturated");
      time(&lastSaturatedLog_);
    }
  } else {
    if (isSaturated_) {
      zxlogf(INFO, "sensor is no longer saturated");
    }
  }
  isSaturated_ = saturatedReading;

  return zx::ok(report);
}

int Tcs3400Device::Thread() {
  // Both polling and interrupts are supported simultaneously
  zx_time_t poll_timeout = ZX_TIME_INFINITE;
  zx_time_t irq_rearm_timeout = ZX_TIME_INFINITE;
  for (;;) {
    zx_port_packet_t packet;
    zx_time_t timeout = std::min(poll_timeout, irq_rearm_timeout);
    zx_status_t status = port_.wait(zx::time(timeout), &packet);
    if (status != ZX_OK && status != ZX_ERR_TIMED_OUT) {
      zxlogf(ERROR, "port wait failed: %d", status);
      return thrd_error;
    }

    if (status == ZX_ERR_TIMED_OUT) {
      if (timeout == irq_rearm_timeout) {
        packet.key = TCS_REARM_IRQ;
      } else {
        packet.key = TCS_POLL;
      }
    }

    Tcs3400FeatureReport feature_report;
    {
      fbl::AutoLock lock(&feature_lock_);
      feature_report = feature_rpt_;
    }

    switch (packet.key) {
      case TCS_SHUTDOWN:
        zxlogf(INFO, "shutting down");
        return thrd_success;
      case TCS_CONFIGURE:
        if (feature_report.report_interval_us == 0) {  // per spec 0 is device's default
          poll_timeout = ZX_TIME_INFINITE;             // we define the default as no polling
        } else {
          poll_timeout = zx_deadline_after(ZX_USEC(feature_report.report_interval_us));
        }

        {
          uint8_t control_reg = 0;
          // clang-format off
          if (feature_report.sensitivity == 4)  control_reg = 1;
          if (feature_report.sensitivity == 16) control_reg = 2;
          if (feature_report.sensitivity == 64) control_reg = 3;
          // clang-format on

          again_ = static_cast<uint8_t>(feature_report.sensitivity);

          const int64_t atime =
              feature_report.integration_time_us / kIntegrationTimeStepSizeMicroseconds;
          atime_ = static_cast<uint8_t>(kMaxIntegrationTimeStep - atime);

          struct Setup {
            uint8_t cmd;
            uint8_t val;
          } __PACKED setup[] = {
              {TCS_I2C_ENABLE,
               TCS_I2C_ENABLE_POWER_ON | TCS_I2C_ENABLE_ADC_ENABLE | TCS_I2C_ENABLE_INT_ENABLE},
              {TCS_I2C_AILTL, GET_BYTE(feature_report.threshold_low, 0)},
              {TCS_I2C_AILTH, GET_BYTE(feature_report.threshold_low, 8)},
              {TCS_I2C_AIHTL, GET_BYTE(feature_report.threshold_high, 0)},
              {TCS_I2C_AIHTH, GET_BYTE(feature_report.threshold_high, 8)},
              {TCS_I2C_PERS, SAMPLES_TO_TRIGGER},
              {TCS_I2C_CONTROL, control_reg},
              {TCS_I2C_ATIME, atime_},
          };
          for (const auto& i : setup) {
            status = WriteReg(i.cmd, i.val);
            if (status != ZX_OK) {
              zxlogf(ERROR, "i2c_write_sync failed: %d", status);
              break;  // do not exit thread, future transactions may succeed
            }
          }
        }
        break;
      case TCS_INTERRUPT:
        zx_interrupt_ack(irq_.get());  // rearm interrupt at the IRQ level

        {
          const zx::status<Tcs3400InputReport> report = ReadInputRpt();
          if (report.is_error()) {
            irq_rearm_timeout = zx_deadline_after(INTERRUPTS_HYSTERESIS);
            break;
          }
          if (feature_report.reporting_state ==
              fuchsia_input_report::wire::SensorReportingState::kReportNoEvents) {
            irq_rearm_timeout = zx_deadline_after(INTERRUPTS_HYSTERESIS);
            break;
          }

          if (report->illuminance > feature_report.threshold_high ||
              report->illuminance < feature_report.threshold_low) {
            readers_.SendReportToAllReaders(*report);
          }

          fbl::AutoLock lock(&input_lock_);
          input_rpt_ = *report;
        }

        irq_rearm_timeout = zx_deadline_after(INTERRUPTS_HYSTERESIS);
        break;
      case TCS_REARM_IRQ:
        // rearm interrupt at the device level
        {
          status = WriteReg(TCS_I2C_AICLEAR, 0x00);
          if (status != ZX_OK) {
            zxlogf(ERROR, "i2c_write_sync failed: %d", status);
            // Continue on error, future transactions may succeed
          }
        }

        irq_rearm_timeout = ZX_TIME_INFINITE;
        break;
      case TCS_POLL:
        if (feature_report.reporting_state !=
            fuchsia_input_report::wire::SensorReportingState::kReportAllEvents) {
          break;
        }

        {
          const zx::status<Tcs3400InputReport> report = ReadInputRpt();
          if (report.is_ok()) {
            readers_.SendReportToAllReaders(*report);
            fbl::AutoLock lock(&input_lock_);
            input_rpt_ = *report;
          }
        }

        poll_timeout += ZX_USEC(feature_report.report_interval_us);
        zx_time_t now = zx_clock_get_monotonic();
        if (now > poll_timeout) {
          poll_timeout = zx_deadline_after(ZX_USEC(feature_report.report_interval_us));
        }
        break;
    }
  }
  return thrd_success;
}

void Tcs3400Device::GetInputReportsReader(GetInputReportsReaderRequestView request,
                                          GetInputReportsReaderCompleter::Sync& completer) {
  readers_.CreateReader(loop_.dispatcher(), std::move(request->reader));
  sync_completion_signal(&next_reader_wait_);  // Only for tests.
}

void Tcs3400Device::GetDescriptor(GetDescriptorRequestView request,
                                  GetDescriptorCompleter::Sync& completer) {
  using SensorAxisVector = fidl::VectorView<fuchsia_input_report::wire::SensorAxis>;

  fidl::Arena<kFeatureAndDescriptorBufferSize> allocator;

  fuchsia_input_report::wire::DeviceInfo device_info;
  device_info.vendor_id = static_cast<uint32_t>(fuchsia_input_report::wire::VendorId::kGoogle);
  device_info.product_id =
      static_cast<uint32_t>(fuchsia_input_report::wire::VendorGoogleProductId::kAmsLightSensor);

  auto sensor_axes = SensorAxisVector(allocator, 4);
  sensor_axes[0] = MakeLightSensorAxis(fuchsia_input_report::wire::SensorType::kLightIlluminance);
  sensor_axes[1] = MakeLightSensorAxis(fuchsia_input_report::wire::SensorType::kLightRed);
  sensor_axes[2] = MakeLightSensorAxis(fuchsia_input_report::wire::SensorType::kLightGreen);
  sensor_axes[3] = MakeLightSensorAxis(fuchsia_input_report::wire::SensorType::kLightBlue);

  const auto input_descriptor =
      fuchsia_input_report::wire::SensorInputDescriptor(allocator).set_values(allocator,
                                                                              sensor_axes);

  auto sensitivity_axes = SensorAxisVector(allocator, 1);
  sensitivity_axes[0] = {
      .axis = kSensitivityAxis,
      .type = fuchsia_input_report::wire::SensorType::kLightIlluminance,
  };

  auto threshold_high_axes = SensorAxisVector(allocator, 1);
  threshold_high_axes[0] =
      MakeLightSensorAxis(fuchsia_input_report::wire::SensorType::kLightIlluminance);

  auto threshold_low_axes = SensorAxisVector(allocator, 1);
  threshold_low_axes[0] =
      MakeLightSensorAxis(fuchsia_input_report::wire::SensorType::kLightIlluminance);

  const auto feature_descriptor = fuchsia_input_report::wire::SensorFeatureDescriptor(allocator)
                                      .set_report_interval(allocator, kReportIntervalAxis)
                                      .set_supports_reporting_state(allocator, true)
                                      .set_sensitivity(allocator, sensitivity_axes)
                                      .set_threshold_high(allocator, threshold_high_axes)
                                      .set_threshold_low(allocator, threshold_low_axes)
                                      .set_sampling_rate(allocator, kSamplingRateAxis);

  const auto sensor_descriptor = fuchsia_input_report::wire::SensorDescriptor(allocator)
                                     .set_input(allocator, input_descriptor)
                                     .set_feature(allocator, feature_descriptor);

  const auto descriptor = fuchsia_input_report::wire::DeviceDescriptor(allocator)
                              .set_device_info(allocator, device_info)
                              .set_sensor(allocator, sensor_descriptor);

  completer.Reply(descriptor);
}

void Tcs3400Device::SendOutputReport(SendOutputReportRequestView request,
                                     SendOutputReportCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Tcs3400Device::GetFeatureReport(GetFeatureReportRequestView request,
                                     GetFeatureReportCompleter::Sync& completer) {
  fbl::AutoLock lock(&feature_lock_);
  fidl::Arena<kFeatureAndDescriptorBufferSize> allocator;
  completer.ReplySuccess(feature_rpt_.ToFidlFeatureReport(allocator));
}

void Tcs3400Device::SetFeatureReport(SetFeatureReportRequestView request,
                                     SetFeatureReportCompleter::Sync& completer) {
  const auto& report = request->report;
  if (!report.has_sensor()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (!report.sensor().has_report_interval() || report.sensor().report_interval() < 0) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (!report.sensor().has_sensitivity() || report.sensor().sensitivity().count() != 1 ||
      !FeatureValueValid(report.sensor().sensitivity()[0], kSensitivityAxis)) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  const int64_t gain = report.sensor().sensitivity()[0];
  if (!(gain == 1 || gain == 4 || gain == 16 || gain == 64)) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (!report.sensor().has_threshold_high() || report.sensor().threshold_high().count() != 1 ||
      !FeatureValueValid(report.sensor().threshold_high()[0], kLightSensorAxis)) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (!report.sensor().has_threshold_low() || report.sensor().threshold_low().count() != 1 ||
      !FeatureValueValid(report.sensor().threshold_low()[0], kLightSensorAxis)) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  if (!report.sensor().has_sampling_rate()) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  const int64_t atime = report.sensor().sampling_rate() / kIntegrationTimeStepSizeMicroseconds;
  if (atime < 1 || atime > 256) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  {
    fbl::AutoLock lock(&feature_lock_);
    feature_rpt_.report_interval_us = report.sensor().report_interval();
    feature_rpt_.reporting_state = report.sensor().reporting_state();
    feature_rpt_.sensitivity = report.sensor().sensitivity()[0];
    feature_rpt_.threshold_high = report.sensor().threshold_high()[0];
    feature_rpt_.threshold_low = report.sensor().threshold_low()[0];
    feature_rpt_.integration_time_us = atime * kIntegrationTimeStepSizeMicroseconds;
  }

  zx_port_packet packet = {TCS_CONFIGURE, ZX_PKT_TYPE_USER, ZX_OK, {}};
  zx_status_t status = port_.queue(&packet);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    zxlogf(ERROR, "zx_port_queue failed: %d", status);
    completer.ReplyError(status);
  }
}

void Tcs3400Device::GetInputReport(GetInputReportRequestView request,
                                   GetInputReportCompleter::Sync& completer) {
  if (request->device_type != fuchsia_input_report::wire::DeviceType::kSensor) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  {
    fbl::AutoLock lock(&feature_lock_);
    if (feature_rpt_.reporting_state !=
        fuchsia_input_report::wire::SensorReportingState::kReportAllEvents) {
      // Light sensor data isn't continuously being read -- the data we have might be far out of
      // date, and we can't block to read new data from the sensor.
      completer.ReplyError(ZX_ERR_BAD_STATE);
      return;
    }
  }

  fidl::Arena<kFeatureAndDescriptorBufferSize> allocator;
  fuchsia_input_report::wire::InputReport report(allocator);

  {
    fbl::AutoLock lock(&input_lock_);
    if (!input_rpt_.is_valid()) {
      // The driver is in the right mode, but hasn't had a chance to read from the sensor yet.
      completer.ReplyError(ZX_ERR_SHOULD_WAIT);
      return;
    }
    input_rpt_.ToFidlInputReport(report, allocator);
  }

  completer.ReplySuccess(report);
}

void Tcs3400Device::WaitForNextReader() {
  sync_completion_wait(&next_reader_wait_, ZX_TIME_INFINITE);
  sync_completion_reset(&next_reader_wait_);
}

// static
zx::status<Tcs3400Device*> Tcs3400Device::CreateAndGetDevice(void* ctx, zx_device_t* parent) {
  ddk::I2cChannel channel(parent, "i2c");
  if (!channel.is_valid()) {
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  ddk::GpioProtocolClient gpio(parent, "gpio");
  if (!gpio.is_valid()) {
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  zx::port port;
  zx_status_t status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "port_create failed: %d", status);
    return zx::error(status);
  }

  auto dev = std::make_unique<tcs::Tcs3400Device>(parent, channel, gpio, std::move(port));
  status = dev->Bind();
  if (status != ZX_OK) {
    zxlogf(ERROR, "bind failed: %d", status);
    return zx::error(status);
  }

  status = dev->DdkAdd("tcs-3400");
  if (status != ZX_OK) {
    zxlogf(ERROR, "DdkAdd failed: %d", status);
    return zx::error(status);
  }

  // devmgr is now in charge of the memory for dev
  return zx::ok(dev.release());
}

zx_status_t Tcs3400Device::Create(void* ctx, zx_device_t* parent) {
  auto status = CreateAndGetDevice(ctx, parent);
  return status.is_error() ? status.error_value() : ZX_OK;
}

zx_status_t Tcs3400Device::InitGain(uint8_t gain) {
  if (!(gain == 1 || gain == 4 || gain == 16 || gain == 64)) {
    zxlogf(WARNING, "Invalid gain (%u) using gain = 1", gain);
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

  auto status = WriteReg(TCS_I2C_CONTROL, reg);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Setting gain failed %d", status);
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
    zxlogf(ERROR, "Getting metadata failed %d", status);
    return status;
  }

  // ATIME = 256 - Integration Time / 2.78 ms.
  int64_t atime = parameters.integration_time_us / kIntegrationTimeStepSizeMicroseconds;
  if (atime < kMinIntegrationTimeStep || atime > kMaxIntegrationTimeStep) {
    atime = kMaxIntegrationTimeStep - 1;
    zxlogf(WARNING, "Invalid integration time (%u) using atime = 1",
           parameters.integration_time_us);
  }
  atime_ = static_cast<uint8_t>(kMaxIntegrationTimeStep - atime);

  zxlogf(DEBUG, "atime (%u)", atime_);
  {
    status = WriteReg(TCS_I2C_ATIME, atime_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Setting integration time failed %d", status);
      return status;
    }
  }

  status = InitGain(parameters.gain);
  if (status != ZX_OK) {
    return status;
  }

  // Set the default features and send a configuration packet.
  {
    fbl::AutoLock lock(&feature_lock_);
    // The device will trigger an interrupt outside the thresholds.  These default threshold
    // values effectively disable interrupts since we can't be outside this range, interrupts
    // get effectively enabled when we configure a range that could trigger.
    feature_rpt_.threshold_low = 0x0000;
    feature_rpt_.threshold_high = 0xFFFF;
    feature_rpt_.sensitivity = again_;
    feature_rpt_.report_interval_us = parameters.polling_time_us;
    feature_rpt_.reporting_state =
        fuchsia_input_report::wire::SensorReportingState::kReportAllEvents;
    feature_rpt_.integration_time_us = atime * kIntegrationTimeStepSizeMicroseconds;
  }
  zx_port_packet packet = {TCS_CONFIGURE, ZX_PKT_TYPE_USER, ZX_OK, {}};
  status = port_.queue(&packet);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_port_queue failed: %d", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t Tcs3400Device::ReadReg(uint8_t reg, uint8_t& output_value) {
  uint8_t write_buffer[] = {reg};
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret = i2c_.WriteReadSyncRetries(write_buffer, countof(write_buffer), &output_value,
                                       sizeof(uint8_t), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "I2C write reg 0x%02X error %d, %d retries", reg, ret.status, ret.retries);
  }
  return ret.status;
}

zx_status_t Tcs3400Device::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buffer[] = {reg, value};
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret =
      i2c_.WriteSyncRetries(write_buffer, countof(write_buffer), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "I2C write reg 0x%02X error %d, %d retries", reg, ret.status, ret.retries);
  }
  return ret.status;
}

zx_status_t Tcs3400Device::Bind() {
  {
    gpio_.ConfigIn(GPIO_NO_PULL);

    auto status = gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &irq_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "gpio_get_interrupt failed: %d", status);
      return status;
    }
  }

  zx_status_t status = irq_.bind(port_, TCS_INTERRUPT, 0);
  if (status != ZX_OK) {
    zxlogf(ERROR, "zx_interrupt_bind failed: %d", status);
    return status;
  }

  status = InitMetadata();
  if (status != ZX_OK) {
    return status;
  }

  int rc = thrd_create_with_name(
      &thread_, [](void* arg) -> int { return reinterpret_cast<Tcs3400Device*>(arg)->Thread(); },
      reinterpret_cast<void*>(this), "tcs3400-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  if ((status = loop_.StartThread("tcs3400-reader-thread")) != ZX_OK) {
    zxlogf(ERROR, "failed to start loop: %d", status);
    ShutDown();
    return status;
  }

  return ZX_OK;
}

void Tcs3400Device::ShutDown() {
  zx_port_packet packet = {TCS_SHUTDOWN, ZX_PKT_TYPE_USER, ZX_OK, {}};
  zx_status_t status = port_.queue(&packet);
  ZX_ASSERT(status == ZX_OK);
  if (thread_) {
    thrd_join(thread_, nullptr);
  }
  irq_.destroy();
  loop_.Shutdown();
}

void Tcs3400Device::DdkUnbind(ddk::UnbindTxn txn) {
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

ZIRCON_DRIVER(tcs3400_light, tcs::driver_ops, "zircon", "0.1");
