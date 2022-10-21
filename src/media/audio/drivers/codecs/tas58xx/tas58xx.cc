// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas58xx.h"

#include <lib/async/cpp/task.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/ddk/trace/event.h>
#include <lib/fit/defer.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "src/media/audio/drivers/codecs/tas58xx/ti_tas58xx-bind.h"

namespace {
// clang-format off
// Book 0
constexpr uint8_t kRegSelectPage  = 0x00;
constexpr uint8_t kRegReset       = 0x01;
constexpr uint8_t kRegDeviceCtrl1 = 0x02;
constexpr uint8_t kRegDeviceCtrl2 = 0x03;
constexpr uint8_t kRegSapCtrl1    = 0x33;
constexpr uint8_t kRegSapCtrl2    = 0x34;
constexpr uint8_t kRegDigitalVol  = 0x4c;
constexpr uint8_t kRegDspMisc     = 0x66;
constexpr uint8_t kRegChanFault    = 0x70;
constexpr uint8_t kRegGlobalFault1 = 0x71;
constexpr uint8_t kRegGlobalFault2 = 0x72;
constexpr uint8_t kRegOtWarning    = 0x73;
constexpr uint8_t kRegClearFault  = 0x78;
constexpr uint8_t kRegSelectBook  = 0x7f;

constexpr uint8_t kRegResetRegsAndModulesCtrl  = 0x11;
constexpr uint8_t kRegDeviceCtrl1BitsPbtlMode  = 0x04;
constexpr uint8_t kRegDeviceCtrl1Bits1SpwMode  = 0x01;
constexpr uint8_t kRegSapCtrl1Bits16bits       = 0x00;
constexpr uint8_t kRegSapCtrl1Bits32bits       = 0x03;
constexpr uint8_t kRegSapCtrl1BitsTdmSmallFs   = 0x14;
constexpr uint8_t kRegDeviceCtrl2BitsDeepSleep = 0x00;
constexpr uint8_t kRegDeviceCtrl2BitsHiZ       = 0x02;
constexpr uint8_t kRegDeviceCtrl2BitsPlay      = 0x03;
constexpr uint8_t kRegDieId                    = 0x67;
constexpr uint8_t kRegClearFaultBitsAnalog     = 0x80;

// Book 0x8c
constexpr uint8_t kRegAgl = 0x68;
constexpr uint8_t kRegAglEnableBitByte0 = 0x80;
// clang-format on

}  // namespace

namespace audio {

namespace audio_fidl = ::fuchsia::hardware::audio;
namespace signal_fidl = ::fuchsia::hardware::audio::signalprocessing;

// TODO(104023): Add handling for the other formats supported by this hardware.
static const std::vector<uint32_t> kSupportedDaiNumberOfChannels = {2, 4};
static const std::vector<SampleFormat> kSupportedDaiSampleFormats = {SampleFormat::PCM_SIGNED};
static const std::vector<FrameFormat> kSupportedDaiFrameFormats = {FrameFormat::I2S,
                                                                   FrameFormat::TDM1};
static const std::vector<uint32_t> kSupportedDaiRates = {48'000, 96'000};  // FS_MODE = Auto.
static const std::vector<uint8_t> kSupportedDaiBitsPerSlot = {16, 32};
static const std::vector<uint8_t> kSupportedDaiBitsPerSample = {16, 32};
static const audio::DaiSupportedFormats kSupportedDaiDaiFormats = {
    .number_of_channels = kSupportedDaiNumberOfChannels,
    .sample_formats = kSupportedDaiSampleFormats,
    .frame_formats = kSupportedDaiFrameFormats,
    .frame_rates = kSupportedDaiRates,
    .bits_per_slot = kSupportedDaiBitsPerSlot,
    .bits_per_sample = kSupportedDaiBitsPerSample,
};

Tas58xx::Tas58xx(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient fault_gpio)
    : SimpleCodecServer(device),
      i2c_(std::move(i2c)),
      fault_gpio_(std::move(fault_gpio)),
      inspect_reporter_(Tas58xxInspect(inspect(), "tas58xx")) {
  size_t actual = 0;
  auto status = device_get_metadata(parent(), DEVICE_METADATA_PRIVATE, &metadata_,
                                    sizeof(metadata_), &actual);
  if (status != ZX_OK) {
    zxlogf(DEBUG, "device_get_metadata failed %d", status);
  }
}

zx_status_t Tas58xx::Stop() {
  // Datasheet states it is required to go to HiZ before going to Deep sleep when coming from Play.
  zx_status_t status = UpdateReg(kRegDeviceCtrl2, 0x3, kRegDeviceCtrl2BitsHiZ);
  if (status != ZX_OK) {
    return status;
  }
  status = UpdateReg(kRegDeviceCtrl2, 0x3, kRegDeviceCtrl2BitsDeepSleep);
  if (status != ZX_OK) {
    return status;
  }
  started_ = false;
  return ZX_OK;
}

zx_status_t Tas58xx::Start() {
  // Datasheet states it is required to go to HiZ before going to Play when coming from Deep sleep.
  zx_status_t status = UpdateReg(kRegDeviceCtrl2, 0x3, kRegDeviceCtrl2BitsHiZ);
  if (status != ZX_OK) {
    return status;
  }
  // Per datasheet, 5ms "for device settle down" after kRegDeviceCtrl2 is set to
  // kRegDeviceCtrl2BitsHiZ before it is set to kRegDeviceCtrl2BitsPlay during startup.
  zx::nanosleep(zx::deadline_after(zx::msec(5)));
  status = UpdateReg(kRegDeviceCtrl2, 0x3, kRegDeviceCtrl2BitsPlay);
  if (status != ZX_OK) {
    return status;
  }
  started_ = true;
  return ZX_OK;
}

zx_status_t Tas58xx::Reset() {
  zx_status_t status = ZX_OK;
  // From the reference manual:
  // "9.5.3.1 Startup Procedures
  // 1. Configure ADR/FAULT pin with proper settings for I2C device address.
  // 2. Bring up power supplies (it does not matter if PVDD or DVDD comes up first).
  // 3. Once power supplies are stable, bring up PDN to High and wait 5ms at least, then start
  // SCLK, LRCLK.
  // 4. Once I2S clocks are stable, set the device into HiZ state and enable DSP via the I2C
  // control port.
  // 5. Wait 5ms at least. Then initialize the DSP Coefficient, then set the device to Play state.
  // 6. The device is now in normal operation."
  // Steps 4+ are execute below.

  // Run the first init sequence from metadata if available otherwise kDefaultsStart.
  if (metadata_.number_of_writes1) {
    for (size_t i = 0; i < metadata_.number_of_writes1; ++i) {
      auto status =
          WriteReg(metadata_.init_sequence1[i].address, metadata_.init_sequence1[i].value);
      if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to write I2C register 0x%02X", metadata_.init_sequence1[i].address);
        return status;
      }
    }
  } else {
    constexpr uint8_t kDefaultsStart[][2] = {
        {kRegSelectPage, 0x00},
        {kRegSelectBook, 0x00},
        {kRegDeviceCtrl2, kRegDeviceCtrl2BitsHiZ},  // Enables DSP.
        {kRegReset, kRegResetRegsAndModulesCtrl},
    };
    for (auto& i : kDefaultsStart) {
      status = WriteReg(i[0], i[1]);
      if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to write I2C register 0x%02X", i[0]);
        return status;
      }
    }
  }

  // Per datasheet, 5ms "for device settle down" after kRegDeviceCtrl2 is set to
  // kRegDeviceCtrl2BitsHiZ before it is set to kRegDeviceCtrl2BitsPlay during startup.
  zx::nanosleep(zx::deadline_after(zx::msec(5)));

  const uint8_t kDefaultsEnd[][2] = {
      {kRegSelectPage, 0x00},
      {kRegSelectBook, 0x00},
      {kRegDeviceCtrl1, static_cast<uint8_t>((metadata_.bridged ? kRegDeviceCtrl1BitsPbtlMode : 0) |
                                             kRegDeviceCtrl1Bits1SpwMode)},

      {kRegDeviceCtrl2, kRegDeviceCtrl2BitsPlay},
      {kRegSelectPage, 0x00},
      {kRegSelectBook, 0x00},
      {kRegClearFault, kRegClearFaultBitsAnalog}};
  for (auto& i : kDefaultsEnd) {
    status = WriteReg(i[0], i[1]);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to write I2C register 0x%02X", i[0]);
      return status;
    }
  }
  constexpr float kDefaultGainDb = -30.f;
  status = SetGain(kDefaultGainDb);
  if (status != ZX_OK) {
    return status;
  }
  return SetMute(true);
}

void Tas58xx::ScheduleFaultPolling() {
  if (!BackgroundFaultPollingIsEnabled())
    return;

  async::PostDelayedTask(
      dispatcher(), [this]() { PeriodicPollFaults(); }, poll_interval_);
}

void Tas58xx::PeriodicPollFaults() {
  zx::time time_now = zx::clock::get_monotonic();
  uint8_t fault_state;
  if (!fault_gpio_.is_valid()) {
    return;  // Only check for periodic faults when the FAULT GPIO is setup.
  }
  auto status = fault_gpio_.Read(&fault_state);
  if (status != ZX_OK) {
    zxlogf(WARNING, "GPIO error while polling fault data");
    inspect_reporter_.ReportGpioError(time_now);
  } else if (fault_state == 0) {  // Active low; 0 means the pin is active!
    // Codec is driving fault pin active.  Read the fault registers.
    fault_info_.i2c_error = (ReadReg(kRegChanFault, &fault_info_.chan_fault) != ZX_OK) ||
                            (ReadReg(kRegGlobalFault1, &fault_info_.global_fault1) != ZX_OK) ||
                            (ReadReg(kRegGlobalFault2, &fault_info_.global_fault2) != ZX_OK) ||
                            (ReadReg(kRegOtWarning, &fault_info_.ot_warning) != ZX_OK);

    // Reset the fault indication at the codec.
    WriteReg(kRegClearFault, kRegClearFaultBitsAnalog);

    // Log the fault based on the data retrieved earlier.
    zxlogf(WARNING, "Codec fault detected");
    if (fault_info_.i2c_error) {
      zxlogf(WARNING, "I2C error while retrieving fault data");
      inspect_reporter_.ReportI2CError(time_now);
    } else {
      if (fault_info_.chan_fault)
        zxlogf(WARNING, "Channel fault seen: %02X", fault_info_.chan_fault);
      if (fault_info_.global_fault1)
        zxlogf(WARNING, "Global fault1 seen: %02X", fault_info_.global_fault1);
      if (fault_info_.global_fault2)
        zxlogf(WARNING, "Global fault2 seen: %02X", fault_info_.global_fault2);
      if (fault_info_.ot_warning)
        zxlogf(WARNING, "OT warning seen: %02X", fault_info_.ot_warning);

      inspect_reporter_.ReportFault(time_now, fault_info_.chan_fault, fault_info_.global_fault1,
                                    fault_info_.global_fault2, fault_info_.ot_warning);
    }
  } else {
    inspect_reporter_.ReportFaultFree(time_now);
  }

  ScheduleFaultPolling();
}

zx::result<DriverIds> Tas58xx::Initialize() {
  ScheduleFaultPolling();
  return zx::ok(DriverIds{
      .vendor_id = PDEV_VID_TI,
      .device_id = PDEV_DID_TI_TAS58xx,
      .instance_count = metadata_.instance_count,
  });
}

zx_status_t Tas58xx::Create(zx_device_t* parent) {
  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "Could not get i2c protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::GpioProtocolClient fault_gpio(parent, "gpio-fault");
  if (!fault_gpio.is_valid()) {
    // It is ok to not have a valid GPIO for fault.
    zxlogf(INFO, "No gpio-fault available");
  }

  return SimpleCodecServer::CreateAndAddToDdk<Tas58xx>(parent, std::move(i2c),
                                                       std::move(fault_gpio));
}

Info Tas58xx::GetInfo() {
  uint8_t die_id = 0;
  zx_status_t status = ReadReg(kRegDieId, &die_id);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to read DIE ID %d", status);
  }
  const char* name = nullptr;
  if (die_id == 0x95) {
    printf("tas58xx: Found TAS5825m\n");
    name = "TAS5825m";
  } else if (die_id == 0x00) {
    printf("tas58xx: Found TAS5805m\n");
    name = "TAS5805m";
  }
  return {.unique_id = "", .manufacturer = "Texas Instruments", .product_name = name};
}

zx_status_t Tas58xx::Shutdown() { return ZX_OK; }

void Tas58xx::SignalProcessingConnect(
    fidl::InterfaceRequest<signal_fidl::SignalProcessing> signal_processing) {
  if (signal_processing_bindings_.size() >= kMaximumNumberOfSignalProcessingConnections) {
    signal_processing.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }
  signal_processing_bindings_.AddBinding(this, std::move(signal_processing), dispatcher());
}

void Tas58xx::GetElements(signal_fidl::SignalProcessing::GetElementsCallback callback) {
  std::vector<signal_fidl::Element> pes;
  {
    signal_fidl::Element pe;
    pe.set_id(kAglPeId);
    pe.set_type(signal_fidl::ElementType::AUTOMATIC_GAIN_LIMITER);
    pe.set_can_disable(true);
    pes.emplace_back(std::move(pe));
  }
  {
    signal_fidl::Element pe;
    pe.set_id(kGainPeId);
    pe.set_type(signal_fidl::ElementType::GAIN);
    pe.set_can_disable(true);
    signal_fidl::Gain gain;
    gain.set_type(signal_fidl::GainType::DECIBELS);
    gain.set_min_gain(kMinGain);
    gain.set_max_gain(kMaxGain);
    gain.set_min_gain_step(kGainStep);
    pe.set_type_specific(signal_fidl::TypeSpecificElement::WithGain(std::move(gain)));
    pes.emplace_back(std::move(pe));
  }
  {
    signal_fidl::Element pe;
    pe.set_id(kMutePeId);
    pe.set_type(signal_fidl::ElementType::MUTE);
    pe.set_can_disable(true);
    pes.emplace_back(std::move(pe));
  }

  signal_fidl::Equalizer equalizer_parameters;

  std::vector<signal_fidl::EqualizerBand> bands;

  for (size_t i = 0; i < kEqualizerNumberOfBands; ++i) {
    signal_fidl::EqualizerBand band;
    band.set_id(i);  // The id we specify is an index to the array of bands.
    bands.push_back(std::move(band));
  }

  equalizer_parameters.set_bands(std::move(bands));
  equalizer_parameters.set_supported_controls(
      signal_fidl::EqualizerSupportedControls::SUPPORTS_TYPE_PEAK |
      signal_fidl::EqualizerSupportedControls::CAN_CONTROL_FREQUENCY);
  equalizer_parameters.set_min_frequency(kEqualizerMinFrequency);
  equalizer_parameters.set_max_frequency(kEqualizerMaxFrequency);
  equalizer_parameters.set_min_gain_db(kEqualizerMinGainDb);
  equalizer_parameters.set_max_gain_db(kEqualizerMaxGainDb);

  signal_fidl::Element pe_eq;
  pe_eq.set_id(kEqPeId);
  pe_eq.set_type(signal_fidl::ElementType::EQUALIZER);
  pe_eq.set_type_specific(
      signal_fidl::TypeSpecificElement::WithEqualizer(std::move(equalizer_parameters)));

  // Only advertise the EQ support for 1 channel configurations.
  if (number_of_channels_ == 1 || metadata_.bridged) {
    pes.emplace_back(std::move(pe_eq));
  }
  signal_fidl::Reader_GetElements_Response response(std::move(pes));
  signal_fidl::Reader_GetElements_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

zx_status_t Tas58xx::SetEqualizerElement(signal_fidl::ElementState state) {
  if (number_of_channels_ != 1 && !metadata_.bridged) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  bool has_valid_equalizer_specific_state = state.has_type_specific() &&
                                            state.type_specific().is_equalizer() &&
                                            state.type_specific().equalizer().has_bands_state();
  if (state.has_enabled() && state.enabled() && !has_valid_equalizer_specific_state) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (state.has_enabled()) {
    equalizer_enabled_ = state.enabled();
  }

  // Update device control and equalizer enable before any other I2C configuration.
  zx_status_t status = ZX_OK;
  if (started_) {
    status = UpdateReg(kRegDeviceCtrl2, 0x3, kRegDeviceCtrl2BitsHiZ);
    if (status != ZX_OK) {
      return status;
    }
    zx::nanosleep(zx::deadline_after(zx::msec(5)));
  }

  auto cleanup = fit::defer([this]() {
    // Update device control enable after equalizer bands I2C configuration.
    if (started_) {
      UpdateReg(kRegDeviceCtrl2, 0x3, kRegDeviceCtrl2BitsPlay);
    }
  });

  // The equalizer_enabled_ state is applied regardless of any previous state and potentially
  // invalid parameters checked below.
  status = WriteReg(kRegDspMisc, 0x06 | !equalizer_enabled_);  // Enable/disable bypass EQ.
  if (status != ZX_OK) {
    return status;
  }
  if (has_valid_equalizer_specific_state) {
    auto& bands_state = state.type_specific().equalizer().bands_state();
    for (size_t i = 0; i < bands_state.size(); ++i) {
      auto& band = bands_state[i];
      // The id we specify is an index to the array of bands.
      if (!band.has_id() || band.id() >= kEqualizerNumberOfBands ||
          // We allow the supported type if specified.
          (band.has_type() && (band.type() != signal_fidl::EqualizerBandType::PEAK)) ||
          // We allow the supported Q if specified.
          (band.has_q() && (band.q() != kSupportedQ))) {
        return ZX_ERR_INVALID_ARGS;
      }

      float gain = band.has_gain_db() ? band.gain_db() : gains_[band.id()];
      uint32_t frequency = band.has_frequency() ? band.frequency() : frequencies_[band.id()];
      if (frequency > kEqualizerMaxFrequency || frequency < kEqualizerMinFrequency ||
          gain > kEqualizerMaxGainDb || gain < kEqualizerMinGainDb) {
        return ZX_ERR_INVALID_ARGS;
      }
      bool enable = band.has_enabled() ? band.enabled() : band_enabled_[band.id()];
      // The id we specify is an index to the array of bands.
      zx_status_t status = SetBand(enable, band.id(), frequency, kSupportedQ, gain);
      if (status != ZX_OK) {
        return ZX_ERR_INTERNAL;
      }

      // With no errors we update our cached parameters.
      frequencies_[band.id()] = frequency;
      gains_[band.id()] = gain;
      band_enabled_[band.id()] = enable;
    }
  }

  if (equalizer_callback_.has_value()) {
    SendEqualizerWatchReply(std::move(equalizer_callback_.value()));
    equalizer_callback_.reset();
    last_equalizer_update_reported_ = true;
  } else {
    last_equalizer_update_reported_ = false;
  }
  return ZX_OK;
}

zx_status_t Tas58xx::SetAutomaticGainLimiterElement(signal_fidl::ElementState state) {
  // If enabled is not present, then perform no operation, we keep the current state.
  if (!state.has_enabled()) {
    return ZX_OK;
  }
  bool enable_agl = state.enabled();

  TRACE_DURATION_BEGIN("tas58xx", "SetAgl", "Enable AGL", enable_agl != last_agl_);
  if (enable_agl != last_agl_) {
    const uint8_t agl_value = 0x40 | (enable_agl ? kRegAglEnableBitByte0 : 0);

    // clang-format off
    uint8_t buffer[] = {
        kRegSelectBook, 0x8c,
        kRegSelectPage, 0x2c,
        kRegAgl, agl_value, 0x00, 0x00, 0x00,
        kRegSelectPage, 0x00,
        kRegSelectBook, 0x00,
    };
    // clang-format on

    fuchsia_hardware_i2c::wire::Transaction ops[5] = {};

    struct {
      fidl::ObjectView<fidl::WireTableFrame<fuchsia_hardware_i2c::wire::Transaction>>
      Transaction() {
        return fidl::ObjectView<fidl::WireTableFrame<fuchsia_hardware_i2c::wire::Transaction>>::
            FromExternal(&transaction);
      }

      fidl::ObjectView<fuchsia_hardware_i2c::wire::DataTransfer> DataTransfer(uint8_t* data,
                                                                              size_t count) {
        write_data = fidl::VectorView<uint8_t>::FromExternal(data, count);
        data_transfer = fuchsia_hardware_i2c::wire::DataTransfer::WithWriteData(
            fidl::ObjectView<fidl::VectorView<uint8_t>>::FromExternal(&write_data));
        return fidl::ObjectView<fuchsia_hardware_i2c::wire::DataTransfer>::FromExternal(
            &data_transfer);
      }

      fidl::WireTableFrame<fuchsia_hardware_i2c::wire::Transaction> transaction;
      fidl::VectorView<uint8_t> write_data;
      fuchsia_hardware_i2c::wire::DataTransfer data_transfer;
    } ops_data[5] = {};

    ops[0] = fuchsia_hardware_i2c::wire::Transaction::ExternalBuilder(ops_data[0].Transaction())
                 .data_transfer(ops_data[0].DataTransfer(&buffer[0], 2))
                 .stop(true)
                 .Build();
    ops[1] = fuchsia_hardware_i2c::wire::Transaction::ExternalBuilder(ops_data[1].Transaction())
                 .data_transfer(ops_data[1].DataTransfer(&buffer[2], 2))
                 .stop(true)
                 .Build();
    ops[2] = fuchsia_hardware_i2c::wire::Transaction::ExternalBuilder(ops_data[2].Transaction())
                 .data_transfer(ops_data[2].DataTransfer(&buffer[4], 5))
                 .stop(true)
                 .Build();
    ops[3] = fuchsia_hardware_i2c::wire::Transaction::ExternalBuilder(ops_data[3].Transaction())
                 .data_transfer(ops_data[3].DataTransfer(&buffer[9], 2))
                 .stop(true)
                 .Build();
    ops[4] = fuchsia_hardware_i2c::wire::Transaction::ExternalBuilder(ops_data[4].Transaction())
                 .data_transfer(ops_data[4].DataTransfer(&buffer[11], 2))
                 .stop(true)
                 .Build();

    auto result =
        i2c_.Transfer(fidl::VectorView<fuchsia_hardware_i2c::wire::Transaction>::FromExternal(
            ops, std::size(ops)));

    signal_fidl::ElementState state;
    state.set_enabled(enable_agl);
    if (agl_callback_.has_value()) {
      (*agl_callback_)(std::move(state));
      last_reported_agl_.emplace(enable_agl);
      agl_callback_.reset();
    }
    last_agl_ = enable_agl;
  }
  // Report the time at which AGL was enabled. This along with the brownout protection driver trace
  // will let us calculate the total latency.
  TRACE_DURATION_END("tas58xx", "SetAgl", "timestamp", zx::clock::get_monotonic().get());
  return ZX_OK;
}

zx_status_t Tas58xx::SetGainElement(signal_fidl::ElementState state) {
  bool has_valid_gain_specific_state = state.has_type_specific() &&
                                       state.type_specific().is_gain() &&
                                       state.type_specific().gain().has_gain();
  if (state.has_enabled()) {
    gain_enabled_ = state.enabled();
  }

  if (has_valid_gain_specific_state) {
    gain_state_.gain = state.type_specific().gain().gain();
  }

  if (state.has_enabled() || has_valid_gain_specific_state) {
    zx_status_t status = SetGain(gain_enabled_ ? gain_state_.gain : 0.0f);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (gain_callback_.has_value()) {
    SendGainWatchReply(std::move(gain_callback_.value()));
    gain_callback_.reset();
    last_gain_update_reported_ = true;
  } else {
    last_gain_update_reported_ = false;
  }
  return ZX_OK;
}

zx_status_t Tas58xx::SetMuteElement(signal_fidl::ElementState state) {
  if (state.has_enabled()) {
    gain_state_.muted = state.enabled();
    zx_status_t status = SetMute(gain_state_.muted);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (mute_callback_.has_value()) {
    SendMuteWatchReply(std::move(mute_callback_.value()));
    mute_callback_.reset();
    last_mute_update_reported_ = true;
  } else {
    last_mute_update_reported_ = false;
  }

  return ZX_OK;
}

void Tas58xx::WatchElementState(uint64_t processing_element_id,
                                signal_fidl::SignalProcessing::WatchElementStateCallback callback) {
  switch (processing_element_id) {
    case kAglPeId:
      if (!last_reported_agl_.has_value() || last_reported_agl_.value() != last_agl_) {
        signal_fidl::ElementState state;
        state.set_enabled(last_agl_);
        callback(std::move(state));
        last_reported_agl_.emplace(last_agl_);
      } else {
        if (agl_callback_.has_value()) {
          zxlogf(WARNING,
                 "Watch request for process element id (%lu) when watch is still in progress",
                 processing_element_id);
        } else {
          agl_callback_.emplace(std::move(callback));
        }
      }
      break;
    case kEqPeId:
      if (!last_equalizer_update_reported_) {
        SendEqualizerWatchReply(std::move(callback));
        last_equalizer_update_reported_ = true;
      } else {
        if (equalizer_callback_.has_value()) {
          zxlogf(WARNING,
                 "Watch request for process element id (%lu) when watch is still in progress",
                 processing_element_id);
        } else {
          equalizer_callback_.emplace(std::move(callback));
        }
      }
      break;
    case kGainPeId:
      if (!last_gain_update_reported_) {
        SendGainWatchReply(std::move(callback));
        last_gain_update_reported_ = true;
      } else {
        if (gain_callback_.has_value()) {
          zxlogf(WARNING,
                 "Watch request for process element id (%lu) when watch is still in progress",
                 processing_element_id);
        } else {
          gain_callback_.emplace(std::move(callback));
        }
      }
      break;
    case kMutePeId:
      if (!last_mute_update_reported_) {
        SendMuteWatchReply(std::move(callback));
        last_mute_update_reported_ = true;
      } else {
        if (mute_callback_.has_value()) {
          zxlogf(WARNING,
                 "Watch request for process element id (%lu) when watch is still in progress",
                 processing_element_id);
        } else {
          mute_callback_.emplace(std::move(callback));
        }
      }
      break;
    default:
      zxlogf(ERROR, "Unknown process element id (%lu) for watch", processing_element_id);
      break;
  }
}

void Tas58xx::SendEqualizerWatchReply(
    signal_fidl::SignalProcessing::WatchElementStateCallback callback) {
  signal_fidl::ElementState state;
  signal_fidl::EqualizerElementState equalizer_state;
  std::vector<signal_fidl::EqualizerBandState> bands_state;

  for (size_t i = 0; i < kEqualizerNumberOfBands; ++i) {
    signal_fidl::EqualizerBandState band;
    band.set_id(i);  // The id we specify is an index to the array of bands.
    band.set_type(signal_fidl::EqualizerBandType::PEAK);
    band.set_frequency(frequencies_[i]);
    band.set_q(kSupportedQ);
    band.set_gain_db(gains_[i]);

    bands_state.push_back(std::move(band));
  }
  equalizer_state.set_bands_state(std::move(bands_state));
  state.set_type_specific(
      signal_fidl::TypeSpecificElementState::WithEqualizer(std::move(equalizer_state)));
  state.set_enabled(equalizer_enabled_);
  callback(std::move(state));
}

void Tas58xx::SendGainWatchReply(
    signal_fidl::SignalProcessing::WatchElementStateCallback callback) {
  signal_fidl::ElementState state;
  signal_fidl::GainElementState gain_state;
  gain_state.set_gain(gain_state_.gain);
  state.set_type_specific(signal_fidl::TypeSpecificElementState::WithGain(std::move(gain_state)));
  state.set_enabled(gain_enabled_);
  callback(std::move(state));
}

void Tas58xx::SendMuteWatchReply(
    signal_fidl::SignalProcessing::WatchElementStateCallback callback) {
  signal_fidl::ElementState state;
  state.set_enabled(gain_state_.muted);
  callback(std::move(state));
}

void Tas58xx::GetTopologies(signal_fidl::SignalProcessing::GetTopologiesCallback callback) {
  std::vector<signal_fidl::EdgePair> edges;
  {
    signal_fidl::EdgePair edge;
    edge.processing_element_id_from = kEqPeId;
    edge.processing_element_id_to = kGainPeId;
    edges.emplace_back(edge);
  }
  {
    signal_fidl::EdgePair edge;
    edge.processing_element_id_from = kGainPeId;
    edge.processing_element_id_to = kMutePeId;
    edges.emplace_back(edge);
  }
  {
    signal_fidl::EdgePair edge;
    edge.processing_element_id_from = kMutePeId;
    edge.processing_element_id_to = kAglPeId;
    edges.emplace_back(edge);
  }

  signal_fidl::Topology topology;
  topology.set_id(kTopologyId);
  topology.set_processing_elements_edge_pairs(edges);

  std::vector<signal_fidl::Topology> topologies;
  topologies.emplace_back(std::move(topology));

  signal_fidl::Reader_GetTopologies_Response response(std::move(topologies));
  signal_fidl::Reader_GetTopologies_Result result;
  result.set_response(std::move(response));
  callback(std::move(result));
}

void Tas58xx::SetTopology(uint64_t topology_id,
                          signal_fidl::SignalProcessing::SetTopologyCallback callback) {
  if (topology_id != kTopologyId) {
    callback(signal_fidl::SignalProcessing_SetTopology_Result::WithErr(ZX_ERR_INVALID_ARGS));
    return;
  }
  callback(signal_fidl::SignalProcessing_SetTopology_Result::WithResponse(
      signal_fidl::SignalProcessing_SetTopology_Response()));
}

void Tas58xx::SetElementState(uint64_t processing_element_id, signal_fidl::ElementState state,
                              signal_fidl::SignalProcessing::SetElementStateCallback callback) {
  zx_status_t status = ZX_OK;
  switch (processing_element_id) {
    case kEqPeId:
      status = SetEqualizerElement(std::move(state));
      break;
    case kAglPeId:
      status = SetAutomaticGainLimiterElement(std::move(state));
      break;
    case kGainPeId:
      status = SetGainElement(std::move(state));
      break;
    case kMutePeId:
      status = SetMuteElement(std::move(state));
      break;
    default:
      status = ZX_ERR_INVALID_ARGS;
      break;
  }

  if (status != ZX_OK) {
    callback(signal_fidl::SignalProcessing_SetElementState_Result::WithErr(std::move(status)));
  } else {
    callback(signal_fidl::SignalProcessing_SetElementState_Result::WithResponse(
        signal_fidl::SignalProcessing_SetElementState_Response()));
  }
}

DaiSupportedFormats Tas58xx::GetDaiFormats() { return kSupportedDaiDaiFormats; }

zx::result<CodecFormatInfo> Tas58xx::SetDaiFormat(const DaiFormat& format) {
  rate_ = format.frame_rate;
  if (!IsDaiFormatSupported(format, kSupportedDaiDaiFormats)) {
    zxlogf(ERROR, "unsupported format");
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // In bridged, only the left is channel supported, from the datasheet:
  // "the input signal to the PBTL amplifier is left frame of I2S or TDM data".
  if (metadata_.bridged &&
      (format.number_of_channels != 2 || (format.channels_to_use_bitmask != 1))) {
    zxlogf(ERROR, "DAI format channels to use not supported in bridged mode %u 0x%lX",
           format.number_of_channels, format.channels_to_use_bitmask);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Only the first 2 bits are ok.
  if (format.number_of_channels == 2 && (format.channels_to_use_bitmask & ~3)) {
    zxlogf(ERROR, "DAI format channels to use not supported %u 0x%lX", format.number_of_channels,
           format.channels_to_use_bitmask);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
  if (format.number_of_channels == 4 && format.channels_to_use_bitmask != 3 &&
      format.channels_to_use_bitmask != 0xc) {
    zxlogf(ERROR, "DAI format channels to use not supported %u 0x%lX", format.number_of_channels,
           format.channels_to_use_bitmask);
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  uint8_t reg_value =
      (format.bits_per_sample == 32 ? kRegSapCtrl1Bits32bits : kRegSapCtrl1Bits16bits) |
      (format.frame_format == FrameFormat::I2S ? 0x00 : kRegSapCtrl1BitsTdmSmallFs);

  auto status = WriteReg(kRegSapCtrl1, reg_value);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = WriteReg(kRegSapCtrl2,
                    (format.number_of_channels == 4 && format.channels_to_use_bitmask == 0xc)
                        ? 2 * format.bits_per_slot
                        : 0x00);
  number_of_channels_ = format.number_of_channels;
  if (status != ZX_OK) {
    return zx::error(status);
  }
  // Run the second initialization sequence from metadata if available.
  // This allows for initialization sequences that are affected by the DAI format to be applied
  // after the DAI format is set.
  for (size_t i = 0; i < metadata_.number_of_writes2; ++i) {
    status = WriteReg(metadata_.init_sequence2[i].address, metadata_.init_sequence2[i].value);
    if (status != ZX_OK) {
      return zx::error(status);
    }
  }
  // Datasheet specifies 5ms when going from HiZ to Play, we do this during turning on (Start).
  CodecFormatInfo info = {};
  info.set_turn_on_delay(zx::msec(5).get());
  return zx::ok(std::move(info));
}

zx_status_t Tas58xx::SetGain(float gain) {
  float clamped_gain = std::clamp(gain, kMinGain, kMaxGain);
  uint8_t gain_reg = static_cast<uint8_t>(48 - clamped_gain * 2);
  zx_status_t status = WriteReg(kRegDigitalVol, gain_reg);
  if (status != ZX_OK) {
    return status;
  }
  gain_state_.gain = clamped_gain;
  return ZX_OK;
}

zx_status_t Tas58xx::SetMute(bool mute) {
  zx_status_t status = UpdateReg(kRegDeviceCtrl2, 0x08, mute ? 0x08 : 0x00);
  if (status != ZX_OK) {
    return status;
  }
  gain_state_.muted = mute;
  return ZX_OK;
}

GainFormat Tas58xx::GetGainFormat() {
  return {
      .min_gain = kMinGain,
      .max_gain = kMaxGain,
      .gain_step = kGainStep,
      .can_mute = true,
      .can_agc = true,
  };
}

void Tas58xx::SetGainState(GainState gain_state) {
  float gain = std::clamp(gain_state.gain, kMinGain, kMaxGain);
  uint8_t gain_reg = static_cast<uint8_t>(48 - gain * 2);
  zx_status_t status = WriteReg(kRegDigitalVol, gain_reg);
  if (status != ZX_OK) {
    return;
  }
  gain_state_ = gain_state;
  static_cast<void>(UpdateReg(kRegDeviceCtrl2, 0x08, gain_state.muted ? 0x08 : 0x00));
}

zx_status_t Tas58xx::SetBand(bool enable, size_t index, uint32_t frequency, float Q,
                             float gain_db) {
  // We use the mechanism documented in "Configure the Coefficients for Digital Biquad Filters in
  // TLV320AIC3xxx Family", April 2010. This works with the TAS5805m, adapted to use the TAS5805m
  // registers 5.27 format. The 5.27 format uses 1 bit is for sign, 4 bits for the positive
  // integer part, and 27 bits for the numbers to the right of the radix point.
  constexpr float kRegisterMaxIntegerPart = 15.f - 0.1f;  // lower than full 4 bits to be safe.

  // Default register configuration to use if enable is false.
  int32_t n0 = 0x0800'0000;
  int32_t n1 = 0;
  int32_t n2 = 0;
  int32_t d1 = 0;
  int32_t d2 = 0;

  // Calculate biquad coefficients if the filter is enabled.
  if (enable) {
    // Intermediate parameters.
    float wo =
        2.f * static_cast<float>(M_PI) * static_cast<float>(frequency) / static_cast<float>(rate_);
    float cosW = std::cosf(wo);
    float sinW = std::sinf(wo);
    float A = std::powf(10.f, gain_db / 40.f);
    float B = std::powf(10.f, gain_db / 20.f);
    float alpha = sinW / (2.f * Q * A);

    // Peaking equalizer coefficients.
    float b0 = B * (1.f + alpha * A);
    float b1 = B * (-2.f * cosW);
    float b2 = B * (1.f - alpha * A);
    float a0 = 1.f + alpha / A;
    float a1 = -2.f * cosW;
    float a2 = 1.f - alpha / A;

    // A/B are normalized versions of a/b such that a0 is 1.f.
    float b0_dsp = b0 / a0;
    float b1_dsp = b1 / a0;
    float b2_dsp = b2 / a0;
    float a1_dsp = -a1 / a0;
    float a2_dsp = -a2 / a0;

    // Make sure we don't go beyond the integer values supported for B coefficients.
    float max_b = std::max(std::max(std::abs(b0_dsp), std::abs(b1_dsp)), std::abs(b2_dsp));
    if (max_b > kRegisterMaxIntegerPart) {
      // We reduce the gain, should not happen for gains below 6dB.
      zxlogf(WARNING, "Equalizer band adjustment beyond supported range (Bx=%f)", max_b);
      b0_dsp = b0_dsp / max_b * kRegisterMaxIntegerPart;
      b1_dsp = b1_dsp / max_b * kRegisterMaxIntegerPart;
      b2_dsp = b2_dsp / max_b * kRegisterMaxIntegerPart;
    }

    // Convert parameters to 5.27 notation.
    float range = static_cast<float>(0x0800'0000);
    n0 = static_cast<int32_t>(std::floorf(b0_dsp * range));
    n1 = static_cast<int32_t>(std::floorf(b1_dsp * range));
    n2 = static_cast<int32_t>(std::floorf(b2_dsp * range));
    d1 = static_cast<int32_t>(std::floorf(a1_dsp * range));
    d2 = static_cast<int32_t>(std::floorf(a2_dsp * range));
  }

  // With kEqualizerNumberOfBands == 5, this equation works, more bands requires other logic.
  static_assert(kEqualizerNumberOfBands == 5);
  uint8_t first_reg_page = 0x24;
  uint8_t first_reg_address = static_cast<uint8_t>(0x18 + index * 20);

  // Create single I2C write for all coefficients for one band, must use a single write.
  uint8_t band_regs[] = {first_reg_address,

                         static_cast<uint8_t>((n0 >> 24)),
                         static_cast<uint8_t>((n0 >> 16) & 0xff),
                         static_cast<uint8_t>((n0 >> 8) & 0xff),
                         static_cast<uint8_t>((n0 >> 0) & 0xff),

                         static_cast<uint8_t>((n1 >> 24) & 0xff),
                         static_cast<uint8_t>((n1 >> 16) & 0xff),
                         static_cast<uint8_t>((n1 >> 8) & 0xff),
                         static_cast<uint8_t>((n1 >> 0) & 0xff),

                         static_cast<uint8_t>((n2 >> 24) & 0xff),
                         static_cast<uint8_t>((n2 >> 16) & 0xff),
                         static_cast<uint8_t>((n2 >> 8) & 0xff),
                         static_cast<uint8_t>((n2 >> 0) & 0xff),

                         static_cast<uint8_t>((d1 >> 24) & 0xff),
                         static_cast<uint8_t>((d1 >> 16) & 0xff),
                         static_cast<uint8_t>((d1 >> 8) & 0xff),
                         static_cast<uint8_t>((d1 >> 0) & 0xff),

                         static_cast<uint8_t>((d2 >> 24) & 0xff),
                         static_cast<uint8_t>((d2 >> 16) & 0xff),
                         static_cast<uint8_t>((d2 >> 8) & 0xff),
                         static_cast<uint8_t>((d2 >> 0) & 0xff)};

  // Use biquad filter 15 for gain compensation.
  // We add up the negative of all the gains and apply it as a gain to the last filter available.
  float delta_gain = 0.f;
  for (size_t i = 0; i < kEqualizerNumberOfBands; ++i) {
    if (i == index) {
      if (enable) {
        delta_gain -= gain_db;
      }
    } else if (band_enabled_[i]) {
      delta_gain -= gains_[i];
    }
  }
  float gain_adjust = std::powf(10.f, delta_gain / 20.f);
  if (gain_adjust >= kRegisterMaxIntegerPart) {
    // We reduce the overall gain adjustment, should only happens with bands adding up to less than
    // -24dB, e.g. 5 bands at -6dB = -30dB.
    zxlogf(WARNING, "Equalizer gain adjustment beyond supported range (%f dB)", delta_gain);
    gain_adjust = kRegisterMaxIntegerPart;
  }
  float range = static_cast<float>(0x0800'0000);  // Format is 5.27.
  n0 = static_cast<int32_t>(std::floorf(gain_adjust * range));
  uint8_t gain_reg_page = 0x26;
  uint8_t gain_address = 0x40;
  // Create single I2C write for all coefficients for the gain adjustment filter.
  // We only need to change the first 4 bytes after the address to adjust the gain, but we still
  // need to set the rest of the bytes with 0s (default values). Must use a single write.
  uint8_t gain_regs[] = {
      gain_address,

      static_cast<uint8_t>((n0 >> 24) & 0xFF),
      static_cast<uint8_t>((n0 >> 16) & 0xFF),
      static_cast<uint8_t>((n0 >> 8) & 0xFF),
      static_cast<uint8_t>((n0 >> 0) & 0xFF),

      0,
      0,
      0,
      0,

      0,
      0,
      0,
      0,

      0,
      0,
      0,
      0,

      0,
      0,
      0,
      0,
  };

  // Now we are ready to write both the coefficients for the band and for the gain adjustment.
  auto cleanup = fit::defer([this]() {
    // Attempt to go back to book 0 in case we exit early.
    WriteReg(kRegSelectPage, 0x00);
    WriteReg(kRegSelectBook, 0x00);
  });

  zx_status_t status = WriteReg(kRegSelectPage, 0x00);
  if (status != ZX_OK) {
    return status;
  }
  status = WriteReg(kRegSelectBook, 0xaa);
  if (status != ZX_OK) {
    return status;
  }

  // Issue the band coefficients write.
  status = WriteReg(kRegSelectPage, first_reg_page);
  if (status != ZX_OK) {
    return status;
  }
  if ((status = i2c_.WriteSync(&band_regs[0], std::size(band_regs))) != ZX_OK) {
    zxlogf(WARNING, "Equalizer I2C transaction failure: %s", zx_status_get_string(status));
    return status;
  }

  // Issue the gain adjustment write.
  status = WriteReg(kRegSelectPage, gain_reg_page);
  if (status != ZX_OK) {
    return status;
  }
  if ((status = i2c_.WriteSync(&gain_regs[0], std::size(gain_regs))) != ZX_OK) {
    zxlogf(WARNING, "Equalizer I2C transaction failure: %s", zx_status_get_string(status));
    return status;
  }

  // Back to book 0 and play mode if we are started.
  status = WriteReg(kRegSelectPage, 0x00);
  if (status != ZX_OK) {
    return status;
  }
  status = WriteReg(kRegSelectBook, 0x00);
  if (status != ZX_OK) {
    return status;
  }
  cleanup.cancel();

  return ZX_OK;
}

GainState Tas58xx::GetGainState() { return gain_state_; }

zx_status_t Tas58xx::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buf[2];
  write_buf[0] = reg;
  write_buf[1] = value;
// #define TRACE_I2C
#ifdef TRACE_I2C
  printf("Writing register 0x%02X to value 0x%02X\n", reg, value);
#endif
  return WriteRegs(write_buf, std::size(write_buf));
}

zx_status_t Tas58xx::WriteRegs(uint8_t* regs, size_t count) {
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret = i2c_.WriteSyncRetries(regs, count, kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    if (count == 2) {
      zxlogf(ERROR, "I2C write reg 0x%02X error %d, %d retries", regs[0], ret.status, ret.retries);
    } else {
      zxlogf(ERROR, "I2C write error %d, %d retries", ret.status, ret.retries);
    }
  }
  return ret.status;
}

zx_status_t Tas58xx::ReadReg(uint8_t reg, uint8_t* value) {
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  ddk::I2cChannel::StatusRetries ret =
      i2c_.WriteReadSyncRetries(&reg, 1, value, 1, kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "I2C read reg 0x%02X error %d, %d retries", reg, ret.status, ret.retries);
  }
#ifdef TRACE_I2C
  printf("Read register 0x%02X, value %02X\n", reg, *value);
#endif
  return ret.status;
}

zx_status_t Tas58xx::UpdateReg(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t old_value = 0;
  auto status = ReadReg(reg, &old_value);
  if (status != ZX_OK) {
    return status;
  }
  return WriteReg(reg, (old_value & ~mask) | (value & mask));
}

zx_status_t tas58xx_bind(void* ctx, zx_device_t* parent) { return Tas58xx::Create(parent); }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = tas58xx_bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(ti_tas58xx, audio::driver_ops, "zircon", "0.1");
