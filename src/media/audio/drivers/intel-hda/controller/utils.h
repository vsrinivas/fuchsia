// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_UTILS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_UTILS_H_

#include <lib/ddk/device.h>
#include <lib/fzl/vmar-manager.h>
#include <lib/zx/bti.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <zircon/types.h>

#include <fbl/macros.h>
#include <fbl/ref_ptr.h>
#include <intel-hda/codec-utils/channel.h>

namespace audio {
namespace intel_hda {

// Constants

// HDA controllers can have at most 30 stream contexts.
constexpr size_t MAX_STREAMS_PER_CONTROLLER = 30;
// We potentially need 1 kiB for the CORB and 2 kiB for the RIRB, each 128 byte
// aligned.
constexpr size_t MAPPED_CORB_RIRB_SIZE = 3072;
// Each BDL can be up to 4096 bytes long (256 entries * 16 bytes).
constexpr size_t MAPPED_BDL_SIZE = 4096;

template <typename T>
static inline constexpr T OR(T x, T y) {
  return static_cast<T>(x | y);
}
template <typename T>
static inline constexpr T AND(T x, T y) {
  return static_cast<T>(x & y);
}

// Create a set of driver-wide VMARs that we stash all of our register
// mappings in, in order to make efficient use of kernel PTEs
fbl::RefPtr<fzl::VmarManager> CreateDriverVmars();

struct StreamFormat {
  // Stream format bitfields documented in section 3.7.1
  static constexpr uint16_t FLAG_NON_PCM = (1u << 15);

  constexpr StreamFormat() {}
  explicit constexpr StreamFormat(uint16_t raw_data) : raw_data_(raw_data) {}

  uint32_t BASE() const { return (raw_data_ & (1u << 14)) ? 44100 : 48000; }
  uint32_t CHAN() const { return (raw_data_ & 0xF) + 1; }
  uint32_t DIV() const { return ((raw_data_ >> 8) & 0x7) + 1; }
  uint32_t MULT() const {
    uint32_t bits = (raw_data_ >> 11) & 0x7;
    if (bits >= 4)
      return 0;
    return bits + 1;
  }
  uint32_t BITS_NDX() const { return (raw_data_ >> 4) & 0x7; }
  uint32_t BITS() const {
    switch (BITS_NDX()) {
      case 0:
        return 8u;
      case 1:
        return 16u;
      case 2:
        return 20u;
      case 3:
        return 24u;
      case 4:
        return 32u;
      default:
        return 0u;
    }
  }

  bool is_pcm() const { return (raw_data_ & FLAG_NON_PCM) == 0; }
  uint32_t sample_rate() const { return (BASE() * MULT()) / DIV(); }
  uint32_t channels() const { return CHAN(); }
  uint32_t bits_per_chan() const { return BITS(); }

  uint32_t bytes_per_frame() const {
    uint32_t ret = CHAN();
    switch (BITS_NDX()) {
      case 0:
        return ret;
      case 1:
        return ret << 1;
      case 2:
      case 3:
      case 4:
        return ret << 2;
      default:
        return 0u;
    }
  }

  bool SanityCheck() const {
    if (raw_data_ == 0x8000)
      return true;

    if (raw_data_ & 0x8080)
      return false;

    return (BITS() && MULT());
  }

  uint16_t raw_data_ = 0;
};

}  // namespace intel_hda
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_INTEL_HDA_CONTROLLER_UTILS_H_
