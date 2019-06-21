// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/write_element.h>
#include <wlan/mlme/rates_elements.h>

namespace wlan {

void RatesWriter::WriteSupportedRates(BufferWriter* w) const {
  if (!all_rates_.empty()) {
    size_t num_rates = std::min(all_rates_.size(), kMaxSupportedRatesLen);
    common::WriteSupportedRates(w, all_rates_.subspan(0, num_rates));
  }
}

void RatesWriter::WriteExtendedSupportedRates(BufferWriter* w) const {
  // Don't write the Extended Supported Rates element if everything fits in
  // Supported rates
  if (all_rates_.size() > kMaxSupportedRatesLen) {
    common::WriteExtendedSupportedRates(
        w, all_rates_.subspan(kMaxSupportedRatesLen));
  }
}

}  // namespace wlan
