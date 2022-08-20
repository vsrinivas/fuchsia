// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parse.h"

#include <lib/uart/uart.h>

namespace uart {

using namespace internal;

template <>
std::optional<zbi_dcfg_simple_t> ParseConfig<zbi_dcfg_simple_t>(std::string_view string) {
  zbi_dcfg_simple_t config{};
  if (ParseInts(string, &config.mmio_phys, &config.irq)) {
    return config;
  }
  return {};
}

template <>
void UnparseConfig(const zbi_dcfg_simple_t& config, FILE* out) {
  UnparseInts(out, config.mmio_phys, config.irq);
}

template <>
std::optional<zbi_dcfg_simple_pio_t> ParseConfig<zbi_dcfg_simple_pio_t>(std::string_view string) {
  zbi_dcfg_simple_pio_t config{};
  if (ParseInts(string, &config.base, &config.irq)) {
    return config;
  }
  return {};
}

template <>
void UnparseConfig(const zbi_dcfg_simple_pio_t& config, FILE* out) {
  UnparseInts(out, config.base, config.irq);
}

}  // namespace uart
