// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_THERMAL_INCLUDE_LIB_THERMAL_NTC_H_
#define SRC_DEVICES_LIB_THERMAL_INCLUDE_LIB_THERMAL_NTC_H_

#include <zircon/assert.h>
#include <zircon/types.h>

#include <algorithm>
#include <vector>

#include <ddk/metadata.h>

#define NTC_CHANNELS_METADATA_PRIVATE (0x4e544300 | DEVICE_METADATA_PRIVATE)  // NTCd
#define NTC_PROFILE_METADATA_PRIVATE (0x4e545000 | DEVICE_METADATA_PRIVATE)   // NTPd

namespace thermal {

static constexpr uint32_t kMaxProfileLen = 50;
static constexpr uint32_t kMaxNameLen = 50;

struct NtcTable {
  float temperature_c;
  uint32_t resistance_ohm;
};

struct NtcChannel {
  uint32_t adc_channel;
  uint32_t pullup_ohms;
  uint32_t profile_idx;
  char name[kMaxNameLen];
};

struct NtcInfo {
  char part[kMaxNameLen];
  NtcTable profile[kMaxProfileLen];  // profile table should be sorted in decreasing resistance
};

class Ntc {
 public:
  Ntc(NtcInfo ntc_info, uint32_t pullup_ohms)
      : profile_(ntc_info.profile, ntc_info.profile + kMaxProfileLen), pullup_ohms_(pullup_ohms) {
    // Sort profile table descending by resistance to ensure proper lookup
    auto sort_compare = [](NtcTable const& x, NtcTable const& y) -> bool {
      return x.resistance_ohm > y.resistance_ohm;
    };
    std::sort(profile_.begin(), profile_.end(), sort_compare);
  }
  // we use a normalized sample [0-1] to prevent having to worry about adc resolution
  //  in this library. This assumes the call site will normalize teh value appropriately
  // Since the thermistor is in series with a pullup resistor, we must convert our sample
  //  value to a resistance then lookup in the profile table.
  zx_status_t GetTemperatureCelsius(float norm_sample, float* out) const {
    if ((norm_sample < 0) || (norm_sample > 1.0)) {
      return ZX_ERR_INVALID_ARGS;
    }
    float ratio = -(norm_sample) / (norm_sample - 1);
    float resistance_f = ratio * static_cast<float>(pullup_ohms_);
    if ((resistance_f > static_cast<float>(std::numeric_limits<uint32_t>::max())) ||
        (resistance_f < static_cast<float>(std::numeric_limits<uint32_t>::min()))) {
      return ZX_ERR_INVALID_ARGS;
    }
    return LookupCelsius(static_cast<uint32_t>(resistance_f), out);
  }

  zx_status_t LookupCelsius(uint32_t resistance, float* out) const {
    auto lb_compare = [](NtcTable const& lhs, uint32_t val) -> bool {
      return lhs.resistance_ohm > val;
    };
    auto low = std::lower_bound(profile_.begin(), profile_.end(), resistance, lb_compare);
    size_t idx = (low - profile_.begin());

    if ((idx == 0) || (idx == kMaxProfileLen)) {
      return ZX_ERR_INVALID_ARGS;
    }
    // Since all entries in profile table may not have been used (passed as metadata)
    // check to make sure lookup wasn't out of range of the table.
    if (profile_.at(idx).resistance_ohm == kInvalidResistance) {
      return ZX_ERR_INVALID_ARGS;
    }

    float span =
        static_cast<float>(profile_.at(idx - 1).resistance_ohm - profile_.at(idx).resistance_ohm);

    float scale = static_cast<float>(resistance - profile_.at(idx).resistance_ohm) / span;

    *out = profile_.at(idx).temperature_c -
           scale * (profile_.at(idx).temperature_c - profile_.at(idx - 1).temperature_c);

    return ZX_OK;
  }

 private:
  static constexpr uint32_t kInvalidResistance = 0;
  std::vector<NtcTable> profile_;
  uint32_t pullup_ohms_ = 0;
};
}  // namespace thermal

#endif  // SRC_DEVICES_LIB_THERMAL_INCLUDE_LIB_THERMAL_NTC_H_
