// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_ALC5663_ALC5663_H_
#define SRC_MEDIA_AUDIO_DRIVERS_ALC5663_ALC5663_H_

#include <lib/device-protocol/i2c-channel.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <ddktl/protocol/i2c.h>
#include <fbl/alloc_checker.h>

#include "i2c_client.h"

namespace audio::alc5663 {

class Alc5663Device;
using DeviceType = ddk::Device<Alc5663Device, ddk::Unbindable>;

// ALC5663 uses 16-bit register addresses.
using Alc5663Client = I2cClient<uint16_t>;

class Alc5663Device : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_AUDIO_CODEC> {
 public:
  // Create a new device. Caller retains ownership of raw pointer arguments.
  Alc5663Device(zx_device_t* parent, ddk::I2cChannel channel);
  ~Alc5663Device() = default;

  // Create a new Alc5663Device object, and bind it to the given parent.
  //
  // The parent should expose an I2C protocol communicating with ALC5663 codec
  // hardware.
  //
  // On success, an unowned pointer to the created device will be returned in
  // |created_device|. Ownership of the pointer remains with the DDK.
  static zx_status_t Bind(zx_device_t* parent, Alc5663Device** created_device);

  // Add a created ALC5663 to its parent.
  //
  // The DDK gains ownership of the device until DdkRelease() is called.
  static zx_status_t AddChildToParent(std::unique_ptr<Alc5663Device> device);

  // Initialise the hardware.
  zx_status_t InitializeDevice();

  // Shutdown the hardware.
  void Shutdown();

  // Implementation of |ddk::Unbindable|.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  Alc5663Client client_;
};

// Maximum parameter values for the ALC5663's Phase-locked loop (PLL).
constexpr const uint16_t kPllMaxN = 511;
constexpr const uint16_t kPllMaxK = 31;
constexpr const uint16_t kPllMaxM = 15;

// PLL parameters.
//
// The PLL takes an input clock with frequency F_in and generates a new clock signal
// with frequency F_out, as follows:
//
//   F_out = (F_in * (N + 2)) / ((M + 2) * (K + 2))
//
// The M and K dividers can additionally be bypassed, removing the "(M + 2)"
// or "(K + 2)" factors respectively.
struct PllParameters {
  uint16_t n;
  uint16_t k;
  uint16_t m;
  bool bypass_m;  // If true, don't divide by (M + 2).
  bool bypass_k;  // If true, don't divide by (K + 2).
};

// Input data format.
//
// TODO(fxbug.dev/35648): Allow this to be configured at runtime.
const uint32_t kBitsPerSample = 24;
const uint32_t kBitsPerChannel = 25;  // Pixelbook Eve NHLT configures 25 bits (sic) per channel.
const uint32_t kNumChannels = 2;
const uint32_t kSampleRate = 48'000;

// Calculate phase-locked loop (PLL) parameters.
//
// In particular, we calculate values of N, M and K such that:
//
//   * The output frequency is >= |desired_freq|.
//   * The output frequency is as close as possible to |desired_freq|.
//
// That is, this function will never produce an output frequency smaller
// than |desired_freq|, but may produce one larger if an exact answer is
// not available.
//
// The ALC5663 manual states outputs should be in the range 2.048MHz to 40MHz,
// and that K is typically 2.
//
// We require |input_freq| and |desired_freq| to be > 0.
zx_status_t CalculatePllParams(uint32_t input_freq, uint32_t desired_freq, PllParameters* params);

}  // namespace audio::alc5663

#endif  // SRC_MEDIA_AUDIO_DRIVERS_ALC5663_ALC5663_H_
