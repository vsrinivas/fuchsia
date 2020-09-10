// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "alc5663.h"

#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/i2c.h>
#include <sys/types.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <cmath>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "alc5663_registers.h"

namespace audio::alc5663 {

namespace {
// Return |a|*|b|, ensuring we avoid overflow by insisting the result is
// cast to a type large enough.
constexpr uint64_t SafeMultiply(uint32_t a, uint32_t b) {
  return static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
}
}  // namespace

zx_status_t CalculatePllParams(uint32_t input_freq, uint32_t desired_freq, PllParameters* params) {
  // Ensure input_freq and desired_freq are in range.
  if (input_freq == 0 || desired_freq == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // We fix K to 2 (as suggested by the ALC5663 documentation), and try to
  // find the best values for N and M such that:
  //
  //  * calculated_freq >= desired_freq
  //
  //  * calculated_freq is as close as possible to desired_freq.
  //
  const uint32_t k = 2;               // ALC5663 document recommends to set k = 2.
  bool have_result = false;           // True if |result| contains valid PLL settings.
  PllParameters result = {};          // Best PLL values seen thus far.
  uint64_t best_calculated_freq = 0;  // Output frequency of PLL values in |result|.

  for (uint32_t n = 0; n <= kPllMaxN; n++) {
    // Calculate the optimal value of (M + 2) for this N and K.
    const uint32_t m_plus_two = static_cast<uint32_t>(std::min<uint64_t>(
        SafeMultiply(input_freq, n + 2) / SafeMultiply(desired_freq, k + 2), kPllMaxM + 2));

    // If (m + 2) == 0, then N is too small to we can scale high enough.
    if (m_plus_two == 0) {
      continue;
    }

    // Calculate the actual frequency.
    const uint64_t calculated_freq =
        SafeMultiply(input_freq, n + 2) / SafeMultiply(m_plus_two, k + 2);

    // If this is a better guess than any previous result, keep track of it.
    if (!have_result || calculated_freq < best_calculated_freq) {
      have_result = true;
      result.m = m_plus_two < 2 ? 0 : static_cast<uint16_t>(m_plus_two - 2);
      result.n = static_cast<uint16_t>(n);
      result.k = static_cast<uint16_t>(k);
      result.bypass_m = (m_plus_two == 1);
      result.bypass_k = false;
      best_calculated_freq = calculated_freq;
    }

    // If we have an exact match, we don't need to keep searching.
    if (calculated_freq == desired_freq) {
      break;
    }
  }

  // If we didn't get a result, it means that no matter how high we make N,
  // we still can't get an output clock high enough.
  if (!have_result) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  zxlogf(DEBUG,
         "alc5663 PLL calculation: input frequency=%u, desired frequency=%u, "
         "calculated frequency=%lu, n=%u, m=%u, k=%u, bypass_m=%d, bypass_k=%u\n",
         input_freq, desired_freq, best_calculated_freq, result.n, result.m, k, result.bypass_m,
         false);
  *params = result;
  return ZX_OK;
}

// Setup the device clocks, ready to play and record audio.
zx_status_t SetUpDeviceClocks(Alc5663Client* client, uint32_t sample_rate,
                              uint32_t bclk_frequency) {
  // We need to configure the ALC5663 to have a clock of 256*|sample_rate| for
  // its system clock.
  //
  // The ALC5663 gives us the choice of using MCLK or BCLK. We don't (yet)
  // have a way for the SoC to communicate MCLK to us, so we currently choose
  // to use BCLK as our main clock.
  //
  // Because |bclk_frequency| won't be high enough to provide
  // a 256*|sample_rate| clock, we need to plumb it into a PLL to get the
  // right frequency, resulting in the following timing chain:
  //
  //    (BCLK) ---> (Clock select) ---> (PLL) ---> (Clock select) --.
  //                                                                |
  //            (System clock) <---clk_sys_i2s <--- (Divider) <-----'
  //
  // Once the I2S BCLK gets plumbed through the PLL, the resulting clock isn't
  // synchronized with original clock any longer. When the clocks are not
  // synchronized, the ALC5663 requires us to enable the "asynchronous
  // sampling rate converter" (ASRC).
  //
  // The datasheet is a little unclear about what the system clock needs to be
  // when ASRC is enabled. Section 7.5 suggests the system clock should be
  // `256*|sample_rate|`. Section 7.5.2 suggests when ASRC is enabled, the
  // system clock must be at least `512*|sample_rate`. Empirically, it appears
  // that the clock `clk_sys_pre` needs to be at least `512*|sample_rate|`,
  // while the clock labelled `clk_sys_i2s` needs to be `256*|sample_rate|`.
  // Confusingly, both are called the "system clock" in different parts of the
  // manual.
  //
  // Our final timing is as follows:
  //
  //   BCLK input: |bclk_frequency|
  //
  //   PLL output: 512*|sample_rate|
  //     * (scale from blck_frequency to 512*|sample_rate|)
  //
  //   Divider output: 256*|sample_rate|
  //     * (divide by 2)

  // Configure the device to be externally clocked ("slave mode").
  zx_status_t result = MapRegister<I2s1DigitalInterfaceControlReg>(
      client, [](auto reg) { return reg.set_i2s1_externally_clocked(1); });
  if (result != ZX_OK) {
    return result;
  }

  // Plumb BCLK into the PLL, and set up the system clock to use the PLL
  // output.
  result = MapRegister<GlobalClockControlReg>(client, [](auto reg) {
    return reg.set_pll_source(GlobalClockControlReg::PllSource::BCLK)
        .set_sysclk1_source(GlobalClockControlReg::SysClk1Source::PLL);
  });
  if (result != ZX_OK) {
    return result;
  }

  // Configure the PLL to convert the input clock from |bclk_frequency| to
  // 512*|sample_rate|.
  PllParameters pll_parameters;
  result = CalculatePllParams(bclk_frequency, 512 * sample_rate, &pll_parameters);
  if (result != ZX_OK) {
    zxlogf(ERROR, "alc5663: Could not set up PLL to convert clock from %uHz to %uHz.",
           bclk_frequency, 512 * sample_rate);
    return result;
  }
  result = MapRegister<PllControl1Reg>(client, [&pll_parameters](auto reg) {
    return reg.set_n_code(pll_parameters.n).set_k_code(pll_parameters.k);
  });
  if (result != ZX_OK) {
    return result;
  }
  result = MapRegister<PllControl2Reg>(client, [&pll_parameters](auto reg) {
    return reg.set_m_code(pll_parameters.m)
        .set_bypass_m(pll_parameters.bypass_m ? 1 : 0)
        .set_bypass_k(pll_parameters.bypass_k ? 1 : 0);
  });
  if (result != ZX_OK) {
    return result;
  }

  // Power up the PLL.
  result =
      MapRegister<PowerManagementControl5Reg>(client, [](auto reg) { return reg.set_pow_pll(1); });
  if (result != ZX_OK) {
    return result;
  }

  // Set up the final divider to convert the 512*|sample_rate| clock into
  // a 256*|sample_rate| clock.
  result = MapRegister<AdcDacClockControlReg>(
      client, [](auto reg) { return reg.set_i2s_pre_div(ClockDivisionRate::DivideBy2); });
  if (result != ZX_OK) {
    return result;
  }

  // Enable ASRC mode.
  result = MapRegister<AsrcControl1Reg>(
      client, [](auto reg) { return reg.set_i2s1_asrc(1).set_dac_asrc(1).set_adc_asrc(1); });
  if (result != ZX_OK) {
    return result;
  }
  result = MapRegister<AsrcControl2Reg>(client, [](auto reg) {
    return reg.set_clk_da_filter_source(AsrcControl2Reg::FilterSource::ASRC)
        .set_clk_ad_filter_source(AsrcControl2Reg::FilterSource::ASRC);
  });
  if (result != ZX_OK) {
    return result;
  }
  result = MapRegister<AsrcControl4Reg>(
      client, [](auto reg) { return reg.set_asrc_i2s1_mode(AsrcControl4Reg::kSampleRate48000); });
  if (result != ZX_OK) {
    return result;
  }

  // Activate clocks.
  result = MapRegister<PowerManagementControlMisc>(
      client, [](auto reg) { return reg.set_gating(PowerManagementControlMisc::kEnable); });
  if (result != ZX_OK) {
    return result;
  }
  result =
      MapRegister<GeneralControlReg>(client, [](auto reg) { return reg.set_digital_gate_ctrl(1); });
  if (result != ZX_OK) {
    return result;
  }

  return ZX_OK;
}

// Enable audio output of the ALC5663 codec.
zx_status_t EnableAudioOutput(Alc5663Client* client) {
  // Bypass the output mixers that mix in sidetone, allow L/R channel swaps.
  zx_status_t result = MapRegister<BypassStereoDacMixerControlReg>(
      client, [](auto reg) { return reg.set_dacl1_source(0).set_dacr1_source(0); });
  if (result != ZX_OK) {
    return result;
  }

  // Power on outputs.
  result = MapRegister<PowerManagementControl1Reg>(
      client, [](auto reg) { return reg.set_en_i2s1(1).set_pow_dac_l_1(1).set_pow_dac_r_1(1); });
  if (result != ZX_OK) {
    return result;
  }

  // Power on the amplifiers.
  result = MapRegister<HpAmpControl1Reg>(client, [](auto reg) {
    return reg.set_pow_pump_l_hp(1)
        .set_pow_pump_r_hp(1)
        .set_pow_capless_l(1)
        .set_pow_capless_r(1)
        .set_enable_l_hp(1)
        .set_enable_r_hp(1);
  });
  if (result != ZX_OK) {
    return result;
  }
  result = MapRegister<HpAmpControl2Reg>(
      client, [](auto reg) { return reg.set_output_l_hp(1).set_output_r_hp(1); });
  if (result != ZX_OK) {
    return result;
  }
  result = MapRegister<HpAmpControl3Reg>(
      client, [](auto reg) { return reg.set_pow_reg_l_hp(1).set_pow_reg_r_hp(1); });
  if (result != ZX_OK) {
    return result;
  }
  result = MapRegister<DacRefLdoControlReg>(
      client, [](auto reg) { return reg.set_pow_ldo_dacrefl(1).set_pow_ldo_dacrefr(1); });
  if (result != ZX_OK) {
    return result;
  }

  // Set digital volume to mid-range.
  result = MapRegister<StereoDacDigitalVolumeReg>(client, [](auto reg) {
    return reg.set_vol_dac1_l(StereoDacDigitalVolumeReg::kTargetVolume)
        .set_vol_dac1_r(StereoDacDigitalVolumeReg::kTargetVolume);
  });

  return ZX_OK;
}

Alc5663Device::Alc5663Device(zx_device_t* parent, ddk::I2cChannel channel)
    : DeviceType(parent), client_(channel) {}

zx_status_t Alc5663Device::InitializeDevice() {
  // Reset the device.
  zx_status_t status = WriteRegister(&client_, ResetAndDeviceIdReg{});
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: Could not reset device.");
    return status;
  }

  // Verify vendor ID and version information.
  VendorIdReg vendor{};
  status = ReadRegister(&client_, &vendor);
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: Could not read device vendor ID.");
    return status;
  }
  if (vendor.vendor_id() != VendorIdReg::kVendorRealtek) {
    zxlogf(ERROR, "alc5663: Unsupported device vendor ID: 0x%04x.", vendor.vendor_id());
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Fetch version for logging.
  VersionIdReg version{};
  status = ReadRegister(&client_, &version);
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: Could not read version information.");
    return status;
  }

  // Log vendor and version.
  zxlogf(INFO, "Found ALC5663 codec, vendor 0x%04x, version 0x%04x.", vendor.vendor_id(),
         version.version_id());

  // Power on everything.
  //
  // TODO(ZX-1538): Only turn on subsystems as/if they are needed.
  status = MapRegister<PowerManagementControl1Reg>(&client_, [](auto reg) {
    return reg.set_en_i2s1(1)
        .set_pow_dac_l_1(1)
        .set_pow_dac_r_1(1)
        .set_pow_ldo_adcref(1)
        .set_pow_adc_l(1);
  });
  if (status != ZX_OK) {
    return status;
  }
  status = MapRegister<PowerManagementControl2Reg>(
      &client_, [](auto reg) { return reg.set_pow_adc_filter(1).set_pow_dac_stereo1_filter(1); });
  if (status != ZX_OK) {
    return status;
  }
  status = MapRegister<PowerManagementControl3Reg>(&client_, [](auto reg) {
    return reg.set_pow_vref1(1)
        .set_pow_vref2(1)
        .set_pow_main_bias(1)
        .set_pow_bg_bias(1)
        .set_en_l_hp(1)
        .set_en_r_hp(1);
  });
  if (status != ZX_OK) {
    return status;
  }
  status = MapRegister<PowerManagementControl4Reg>(&client_, [](auto reg) {
    return reg.set_pow_bst1(1).set_pow_micbias1(1).set_pow_micbias2(1).set_pow_recmix1(1);
  });
  if (status != ZX_OK) {
    return status;
  }

  // Setup internal clocks and PLL.
  //
  // TODO(fxbug.dev/35648): Allow this to be configured at runtime.
  status = SetUpDeviceClocks(&client_, /*sample_rate=*/kSampleRate,
                             /*bclk_frequency=*/(kSampleRate * kBitsPerChannel * kNumChannels));
  if (status != ZX_OK) {
    return status;
  }

  // Set up audio outputs.
  status = EnableAudioOutput(&client_);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

void Alc5663Device::Shutdown() {
  // Reset the device.
  //
  // TODO(dgreenway): Power down the device.
  zx_status_t status = WriteRegister(&client_, ResetAndDeviceIdReg{});
  if (status != ZX_OK) {
    zxlogf(WARNING, "alc5663: Failed to reset the device during shutdown.");
  }
}

void Alc5663Device::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void Alc5663Device::DdkRelease() { delete this; }

zx_status_t Alc5663Device::AddChildToParent(std::unique_ptr<Alc5663Device> device) {
  // Add the device.
  zx_status_t status = device->DdkAdd("alc5663");
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: could not add device: %s", zx_status_get_string(status));
    return status;
  }

  (void)device.release();  // `dev` will be deleted when DdkRelease() is called.
  return ZX_OK;
}

zx_status_t Alc5663Device::Bind(zx_device_t* parent, Alc5663Device** created_device) {
  // Get access to the I2C protocol.
  ddk::I2cChannel channel;
  zx_status_t result = ddk::I2cChannel::CreateFromDevice(parent, &channel);
  if (result != ZX_OK) {
    zxlogf(ERROR, "alc5663: could not get I2C protocol from parent device: %s",
           zx_status_get_string(result));
    return result;
  }

  // Create the codec device.
  fbl::AllocChecker ac;
  auto device = std::unique_ptr<Alc5663Device>(new (&ac) Alc5663Device(parent, channel));
  if (!ac.check()) {
    zxlogf(ERROR, "alc5663: out of memory attempting to allocate device");
    return ZX_ERR_NO_MEMORY;
  }

  // Initialise the hardware.
  zx_status_t status = device->InitializeDevice();
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5663: failed to initialize hardware: %s", zx_status_get_string(status));
    return status;
  }

  // Attach to our parent.
  *created_device = device.get();
  return Alc5663Device::AddChildToParent(std::move(device));
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = [](void* /*ctx*/, zx_device_t* parent) {
    Alc5663Device* unused;
    return Alc5663Device::Bind(parent, &unused);
  };
  return ops;
}();

}  // namespace audio::alc5663

// clang-format off
ZIRCON_DRIVER_BEGIN(alc5663, audio::alc5663::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, 0x31304543), // '10EC' (Realtek)
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x35363633), // '5663'
ZIRCON_DRIVER_END(alc5663)
    // clang-format on
