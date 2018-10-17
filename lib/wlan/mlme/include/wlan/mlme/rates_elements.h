// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/element.h>

// Utilities for writing SupportedRates / ExtendedSupportedRates elements

namespace wlan {

class RatesWriter {
 public:
    RatesWriter(const SupportedRate all_rates[], size_t rates_len)
      : all_rates_(all_rates), rates_len_(rates_len) {}

    bool WriteSupportedRates(ElementWriter* w) const;

    bool WriteExtendedSupportedRates(ElementWriter* w) const;

 private:
    const SupportedRate* all_rates_;
    size_t rates_len_;
};

} // namespace wlan
