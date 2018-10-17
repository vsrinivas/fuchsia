// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/rates_elements.h>

namespace wlan {

bool RatesWriter::WriteSupportedRates(ElementWriter* w) const {
    if (all_rates_ == nullptr || rates_len_ == 0) {
        return true;
    }

    size_t num_rates = std::min(rates_len_, SupportedRatesElement::kMaxLen);
    return w->write<SupportedRatesElement>(all_rates_, num_rates);
}

bool RatesWriter::WriteExtendedSupportedRates(ElementWriter* w) const {
    if (all_rates_ == nullptr) {
        return true;
    }

    // Don't write the Extended Supported Rates element if everything fits in Supported rates
    if (rates_len_ <= SupportedRatesElement::kMaxLen) {
        return true;
    }

    const SupportedRate* ext_rates = all_rates_ + SupportedRatesElement::kMaxLen;
    size_t num_rates = rates_len_ - SupportedRatesElement::kMaxLen;

    return w->write<ExtendedSupportedRatesElement>(ext_rates, num_rates);
}

} // namespace wlan
