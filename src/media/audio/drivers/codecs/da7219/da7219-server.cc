// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "da7219-server.h"

#include <lib/zx/clock.h>

#include "da7219-regs.h"

namespace audio::da7219 {

// Core methods.

Core::Core(Logger* logger, fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c, zx::interrupt irq)
    : logger_(logger),
      i2c_(std::move(i2c)),
      irq_(std::move(irq)),
      config_(MakeConfig()),
      loop_(&config_) {
  irq_handler_.set_object(irq_.get());
  irq_handler_.Begin(loop_.dispatcher());
  loop_.StartThread();
}

void Core::PlugDetected(bool plugged, bool with_mic) {
  DA7219_LOG(INFO, "Plug event: %s %s", plugged ? "plugged" : "unplugged",
             with_mic ? "with mic" : "no mic");

  // Enable/disable HP left.
  auto hplctrl = HpLCtrl::Read(i2c_);
  if (!hplctrl.is_ok()) {
    return;
  }
  zx_status_t status = hplctrl
                           ->set_hp_l_amp_en(plugged)  // HP_L_AMP amplifier control.
                           .set_hp_l_amp_oe(plugged)   // Output control, output is driven.
                           .Write(i2c_);
  if (status != ZX_OK)
    return;

  // Enable/disable HP right.
  auto hprctrl = HpRCtrl::Read(i2c_);
  if (!hprctrl.is_ok()) {
    return;
  }
  status = hprctrl
               ->set_hp_r_amp_en(plugged)  // HP_R_AMP amplifier control.
               .set_hp_r_amp_oe(plugged)   // Output control, output is driven.
               .Write(i2c_);
  if (status != ZX_OK)
    return;

  // No errors, now update callbacks. Input is plugged only if the HW detected a 4-pole jack.
  if (plug_callback_input_.has_value()) {
    bool plugged_with_mic = plugged && with_mic;
    (*plug_callback_input_)(plugged_with_mic);
  }

  if (plug_callback_output_.has_value()) {
    (*plug_callback_output_)(plugged);
  }
}

void Core::AddPlugCallback(bool is_input, PlugCallback cb) {
  if (is_input) {
    plug_callback_input_.emplace(std::move(cb));
  } else {
    plug_callback_output_.emplace(std::move(cb));
  }
}

void Core::Shutdown() {
  zx_status_t status = SystemActive::Get().set_system_active(false).Write(i2c_);
  if (status != ZX_OK) {
    DA7219_LOG(ERROR, "Could not deactive the HW: %s", zx_status_get_string(status));
  }

  loop_.Shutdown();
  irq_handler_.Cancel();
  irq_.destroy();
}

zx_status_t Core::Initialize() {
  auto chip_id1 = ChipId1::Read(i2c_);
  if (!chip_id1.is_ok())
    return chip_id1.status_value();

  auto chip_id2 = ChipId2::Read(i2c_);
  if (!chip_id2.is_ok())
    return chip_id2.status_value();

  auto chip_revision = ChipRevision::Read(i2c_);
  if (!chip_revision.is_ok())
    return chip_revision.status_value();

  constexpr uint8_t kSupportedChipId1 = 0x23;
  constexpr uint8_t kSupportedChipId2 = 0x93;
  if (chip_id1->chip_id1() != kSupportedChipId1 || chip_id2->chip_id2() != kSupportedChipId2) {
    DA7219_LOG(ERROR, "Found not supported CHIP ids 0x%02X:0x%02X", chip_id1->chip_id1(),
               chip_id2->chip_id2());
    return ZX_ERR_NOT_SUPPORTED;
  }
  DA7219_LOG(INFO, "Found device ID:0x%02X/0x%02X REV:0x%01X/0x%01X", chip_id1->chip_id1(),
             chip_id2->chip_id2(), chip_revision->chip_major(), chip_revision->chip_minor());

  return ZX_OK;
}

zx_status_t Core::Reset() {
  zx_status_t status = SystemActive::Get().set_system_active(true).Write(i2c_);
  if (status != ZX_OK)
    return status;

  status = PllCtrl::Get()
               .set_pll_mode(PllCtrl::kPllModeSrm)  // Sampling Rate Matching SRM mode.
               // The PLL is enabled, and the system clock tracks WCLK.
               .set_pll_mclk_sqr_en(false)
               .set_pll_indiv(PllCtrl::kPllIndiv18to36MHz)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  // The HP amplifiers are configured to operate in true-ground (Charge Pump) mode.
  status = CpCtrl::Get().set_cp_en(true).set_cp_mchange(CpCtrl::kCpMchangeDacVol).Write(i2c_);
  if (status != ZX_OK)
    return status;

  // Output routing, configure headset output but leave them disabled for AAD (Advanced Accessory
  // Detect).
  status =
      DacLCtrl::Get().set_dac_l_en(true).set_dac_l_mute_en(false).set_dac_l_ramp_en(false).Write(
          i2c_);
  if (status != ZX_OK)
    return status;
  status =
      DacRCtrl::Get().set_dac_r_en(true).set_dac_r_mute_en(false).set_dac_r_ramp_en(false).Write(
          i2c_);
  if (status != ZX_OK)
    return status;
  status = MixoutLSelect::Get().set_mixout_l_mix_select(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixoutRSelect::Get().set_mixout_r_mix_select(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixoutLCtrl::Get().set_mixout_l_amp_en(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixoutRCtrl::Get().set_mixout_r_amp_en(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = HpLCtrl::Get()
               .set_hp_l_amp_en(false)  // HP_L_AMP amplifier control.
               .set_hp_l_amp_mute_en(false)
               .set_hp_l_amp_ramp_en(false)
               .set_hp_l_amp_zc_en(false)
               .set_hp_l_amp_oe(false)  // Output control, output is driven.
               .set_hp_l_amp_min_gain_en(false)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = HpRCtrl::Get()
               .set_hp_r_amp_en(false)  // HP_R_AMP amplifier control.
               .set_hp_r_amp_mute_en(false)
               .set_hp_r_amp_ramp_en(false)
               .set_hp_r_amp_zc_en(false)
               .set_hp_r_amp_oe(false)  // Output control, output is driven.
               .set_hp_r_amp_min_gain_en(false)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  // Input routing, configure headset input with arbitrary gain.
  status = Mic1Gain::Get().set_mic_1_amp_gain(Mic1Gain::k30dB).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = Mic1Ctrl::Get()
               .set_mic_1_amp_en(true)
               .set_mic_1_amp_mute_en(false)
               .set_mic_1_amp_ramp_en(false)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixinLSelect::Get().set_mixin_l_mix_select(true).Write(i2c_);
  if (status != ZX_OK)
    return status;
  status = MixinLCtrl::Get()
               .set_mixin_l_amp_en(true)
               .set_mixin_l_amp_mute_en(false)
               .set_mixin_l_amp_ramp_en(false)
               .set_mixin_l_amp_zc_en(false)
               .set_mixin_l_mix_en(true)
               .Write(i2c_);
  status =
      AdcLCtrl::Get().set_adc_l_en(true).set_adc_l_mute_en(false).set_adc_l_ramp_en(false).Write(
          i2c_);
  if (status != ZX_OK)
    return status;
  status = DigRoutingDai::Get()
               .set_dai_r_src(DigRoutingDai::kAdcLeft)
               .set_dai_l_src(DigRoutingDai::kAdcLeft)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  // Enable AAD (Advanced Accessory Detect).
  status = AccdetConfig1::Get()
               .set_pin_order_det_en(true)
               .set_jack_type_det_en(true)
               .set_mic_det_thresh(AccdetConfig1::kMicDetThresh500Ohms)
               .set_button_config(AccdetConfig1::kButtonConfig10ms)
               .set_accdet_en(true)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  auto status_a = AccdetStatusA::Read(i2c_);
  if (!status_a.is_ok())
    return status_a.error_value();
  PlugDetected(status_a->jack_insertion_sts(), status_a->jack_type_sts());

  // Unmask AAD IRQs.
  status = AccdetIrqMaskA::Get()
               .set_m_jack_detect_comp(false)
               .set_m_jack_removed(false)
               .set_m_jack_inserted(true)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  // Mask all buttons IRQs.
  status = AccdetIrqMaskB::Get()
               .set_m_button_a_release(true)
               .set_m_button_b_release(true)
               .set_m_button_c_release(true)
               .set_m_button_d_release(true)
               .set_m_button_d_pressed(true)
               .set_m_button_c_pressed(true)
               .set_m_button_b_pressed(true)
               .set_m_button_a_pressed(true)
               .Write(i2c_);
  if (status != ZX_OK)
    return status;

  // Clear buttons state.
  return AccdetIrqEventB::Get()
      .set_e_button_a_released(true)
      .set_e_button_b_released(true)
      .set_e_button_c_released(true)
      .set_e_button_d_released(true)
      .set_e_button_d_pressed(true)
      .set_e_button_c_pressed(true)
      .set_e_button_b_pressed(true)
      .set_e_button_a_pressed(true)
      .Write(i2c_);
}

void Core::HandleIrq(async_dispatcher_t* dispatcher, async::IrqBase* irq, zx_status_t status,
                     const zx_packet_interrupt_t* interrupt) {
  if (status != ZX_OK) {
    // Do not log canceled cases which happens too often in particular in test cases.
    if (status != ZX_ERR_CANCELED) {
      DA7219_LOG(ERROR, "IRQ wait: %s", zx_status_get_string(status));
    }
    return;
  }

  auto event_a = AccdetIrqEventA::Read(i2c_);
  if (!event_a.is_ok())
    return;

  auto status_a = AccdetStatusA::Read(i2c_);
  if (!status_a.is_ok())
    return;

  if (event_a->e_jack_detect_complete()) {
    // Only report once we are done with detection.
    PlugDetected(true, status_a->jack_type_sts());
  } else if (event_a->e_jack_removed()) {
    PlugDetected(false, status_a->jack_type_sts());
  }

  irq_.ack();
  status = AccdetIrqEventA::Get()
               .set_e_jack_detect_complete(true)  // Set to clear.
               .set_e_jack_removed(true)          // Set to clear.
               .set_e_jack_inserted(true)         // Set to clear.
               .Write(i2c_);
  if (status != ZX_OK)
    return;
}

// Server methods.

Server::Server(Logger* logger, std::shared_ptr<Core> core, bool is_input)
    : logger_(logger), core_(std::move(core)), is_input_(is_input) {
  core_->AddPlugCallback(is_input_, [this](bool plugged) {
    // Update plug state if we haven't set it yet, or if changed.
    if (!plugged_time_.get() || plugged_ != plugged) {
      plugged_ = plugged;
      plugged_time_ = zx::clock::get_monotonic();
      if (plug_state_completer_) {
        plug_state_updated_ = false;
        fidl::Arena arena;
        auto plug_state = fuchsia_hardware_audio::wire::PlugState::Builder(arena);
        plug_state.plugged(plugged_).plug_state_time(plugged_time_.get());
        plug_state_completer_->Reply(plug_state.Build());
        plug_state_completer_.reset();
      } else {
        plug_state_updated_ = true;
      }
    }
  });
}

void Server::Reset(ResetCompleter::Sync& completer) {
  // Either driver resets the whole core.
  zx_status_t status = core_->Reset();
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  completer.Reply();
}

void Server::GetInfo(GetInfoCompleter::Sync& completer) {
  fuchsia_hardware_audio::wire::CodecInfo info{
      .unique_id = "", .manufacturer = "Dialog", .product_name = "DA7219"};
  completer.Reply(info);
}

void Server::Stop(StopCompleter::Sync& completer) { completer.Close(ZX_ERR_NOT_SUPPORTED); }

void Server::Start(StartCompleter::Sync& completer) {
  completer.Reply({});  // Always started.
}

void Server::GetHealthState(GetHealthStateCompleter::Sync& completer) { completer.Reply({}); }

void Server::IsBridgeable(IsBridgeableCompleter::Sync& completer) { completer.Reply(false); }

void Server::SetBridgedMode(SetBridgedModeRequestView request,
                            SetBridgedModeCompleter::Sync& completer) {
  completer.Close(ZX_ERR_NOT_SUPPORTED);
}

void Server::GetDaiFormats(GetDaiFormatsCompleter::Sync& completer) {
  // TODO(104023): Add handling for the other formats supported by this hardware.
  fidl::Arena arena;
  static std::vector<uint32_t> kChannels = {2};
  static std::vector<fuchsia_hardware_audio::wire::DaiSampleFormat> kSampleFormats = {
      fuchsia_hardware_audio::wire::DaiSampleFormat::kPcmSigned};
  static std::vector<fuchsia_hardware_audio::wire::DaiFrameFormat> kFrameFormats = {
      fuchsia_hardware_audio::wire::DaiFrameFormat::WithFrameFormatStandard(
          fuchsia_hardware_audio::wire::DaiFrameFormatStandard::kI2S)};
  // As secondary, the system clock tracks WCLK (Sampling Rate Matching SRM mode).
  static std::vector<uint32_t> kFrameRates = {8'000,  11'025, 12'000, 16'000, 22'050, 24'000,
                                              32'000, 44'100, 48'000, 88'200, 96'000};
  // In I2S all these bits per slot/sample are supported.
  static std::vector<uint8_t> kBitsPerSlot = {16, 20, 24, 32};
  static std::vector<uint8_t> kBitsPerSample = {16, 20, 24, 32};
  fuchsia_hardware_audio::wire::DaiSupportedFormats formats = {
      .number_of_channels = fidl::VectorView<uint32_t>(arena, kChannels),
      .sample_formats =
          fidl::VectorView<fuchsia_hardware_audio::wire::DaiSampleFormat>(arena, kSampleFormats),
      .frame_formats =
          fidl::VectorView<fuchsia_hardware_audio::wire::DaiFrameFormat>(arena, kFrameFormats),
      .frame_rates = fidl::VectorView<uint32_t>(arena, kFrameRates),
      .bits_per_slot = fidl::VectorView<uint8_t>(arena, kBitsPerSlot),
      .bits_per_sample = fidl::VectorView<uint8_t>(arena, kBitsPerSample),
  };
  std::vector<fuchsia_hardware_audio::wire::DaiSupportedFormats> all_formats;
  all_formats.emplace_back(formats);
  fidl::VectorView<fuchsia_hardware_audio::wire::DaiSupportedFormats> all_formats2(arena,
                                                                                   all_formats);
  completer.ReplySuccess(all_formats2);
}

void Server::SetDaiFormat(SetDaiFormatRequestView request, SetDaiFormatCompleter::Sync& completer) {
  auto format = request->format;
  uint8_t dai_word_length = 0;
  // clang-format off
  switch (format.bits_per_sample) {
    case 16: dai_word_length = DaiCtrl::kDaiWordLength16BitsPerChannel; break;
    case 20: dai_word_length = DaiCtrl::kDaiWordLength20BitsPerChannel; break;
    case 24: dai_word_length = DaiCtrl::kDaiWordLength24BitsPerChannel; break;
    case 32: dai_word_length = DaiCtrl::kDaiWordLength32BitsPerChannel; break;
    default: {
      completer.Close(ZX_ERR_NOT_SUPPORTED);
      return;
    }
  }
  uint8_t frame_rate = 0;
  switch (format.frame_rate) {
    case 8'000: frame_rate = Sr::k8000Hz; break;
    case 11'025: frame_rate = Sr::k11025Hz; break;
    case 12'000: frame_rate = Sr::k12000Hz; break;
    case 16'000: frame_rate = Sr::k16000Hz; break;
    case 22'050: frame_rate = Sr::k22050Hz; break;
    case 24'000: frame_rate = Sr::k24000Hz; break;
    case 32'000: frame_rate = Sr::k32000Hz; break;
    case 44'100: frame_rate = Sr::k44100Hz; break;
    case 48'000: frame_rate = Sr::k48000Hz; break;
    case 88'200: frame_rate = Sr::k88200Hz; break;
    case 96'000: frame_rate = Sr::k96000Hz; break;
    default: {
      completer.Close(ZX_ERR_NOT_SUPPORTED);
      return;
    }
  }
  // clang-format on
  zx_status_t status = DaiCtrl::Get().set_dai_en(false).Write(core_->i2c());
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  status = Sr::Get().set_sr(frame_rate).Write(core_->i2c());
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  status = DaiTdmCtrl::Get()
               .set_dai_tdm_mode_en(false)  // Mode set is I2S, not TDM.
               .set_dai_oe(true)
               .set_dai_tdm_ch_en(DaiTdmCtrl::kLeftChannelAndRightChannelBothEnabled)
               .Write(core_->i2c());
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  status = DaiCtrl::Get()
               .set_dai_en(true)
               .set_dai_ch_num(DaiCtrl::kDaiChNumLeftAndRightChannelsAreEnabled)
               .set_dai_word_length(dai_word_length)
               .set_dai_format(DaiCtrl::kDaiFormatI2sMode)
               .Write(core_->i2c());
  if (status != ZX_OK) {
    completer.Close(status);
    return;
  }
  completer.ReplySuccess({});
}

void Server::GetPlugDetectCapabilities(GetPlugDetectCapabilitiesCompleter::Sync& completer) {
  completer.Reply(fuchsia_hardware_audio::wire::PlugDetectCapabilities::kCanAsyncNotify);
}

void Server::WatchPlugState(WatchPlugStateCompleter::Sync& completer) {
  fidl::Arena arena;
  auto plug_state = fuchsia_hardware_audio::wire::PlugState::Builder(arena);
  plug_state.plugged(plugged_).plug_state_time(plugged_time_.get());
  if (plug_state_updated_) {
    plug_state_updated_ = false;
    completer.Reply(plug_state.Build());
  } else if (!plug_state_completer_) {
    plug_state_completer_.emplace(completer.ToAsync());
  } else {
    DA7219_LOG(WARNING, "Client called WatchPlugState when another hanging get was pending");
  }
}

void Server::SignalProcessingConnect(SignalProcessingConnectRequestView request,
                                     SignalProcessingConnectCompleter::Sync& completer) {}

}  // namespace audio::da7219
