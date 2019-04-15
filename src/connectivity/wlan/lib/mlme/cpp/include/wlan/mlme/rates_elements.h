// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_RATES_ELEMENTS_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_RATES_ELEMENTS_H_

#include <wlan/common/buffer_writer.h>
#include <wlan/common/element.h>
#include <wlan/common/span.h>

// Utilities for writing SupportedRates / ExtendedSupportedRates elements

namespace wlan {

class RatesWriter {
 public:
  explicit RatesWriter(Span<const SupportedRate> all_rates)
      : all_rates_(all_rates) {}

  void WriteSupportedRates(BufferWriter* w) const;

  void WriteExtendedSupportedRates(BufferWriter* w) const;

 private:
  Span<const SupportedRate> all_rates_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_RATES_ELEMENTS_H_
