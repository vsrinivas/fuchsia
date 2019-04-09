// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/buffer_writer.h>
#include <wlan/common/element.h>
#include <wlan/common/span.h>

// Utilities for writing SupportedRates / ExtendedSupportedRates elements

namespace wlan {

class RatesWriter {
 public:
    explicit RatesWriter(Span<const SupportedRate> all_rates) : all_rates_(all_rates) {}

    void WriteSupportedRates(BufferWriter* w) const;

    void WriteExtendedSupportedRates(BufferWriter* w) const;

 private:
    Span<const SupportedRate> all_rates_;
};

} // namespace wlan
