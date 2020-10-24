// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parse.h"

#include <lib/uart/uart.h>

namespace uart {

using namespace internal;

template <>
std::optional<dcfg_simple_t> ParseConfig<dcfg_simple_t>(std::string_view string) {
  dcfg_simple_t config{};
  if (ParseInts(string, &config.mmio_phys, &config.irq)) {
    return config;
  }
  return {};
}

template <>
void UnparseConfig(const dcfg_simple_t& config, FILE* out) {
  UnparseInts(out, config.mmio_phys, config.irq);
}

template <>
std::optional<dcfg_simple_pio_t> ParseConfig<dcfg_simple_pio_t>(std::string_view string) {
  dcfg_simple_pio_t config{};
  if (ParseInts(string, &config.base, &config.irq)) {
    return config;
  }
  return {};
}

template <>
void UnparseConfig(const dcfg_simple_pio_t& config, FILE* out) {
  UnparseInts(out, config.base, config.irq);
}

template <>
std::optional<dcfg_soc_uart_t> ParseConfig<dcfg_soc_uart_t>(std::string_view string) {
  dcfg_soc_uart_t config{};
  if (ParseInts(string, &config.soc_mmio_phys, &config.uart_mmio_phys, &config.irq)) {
    return config;
  }
  return {};
}

template <>
void UnparseConfig(const dcfg_soc_uart_t& config, FILE* out) {
  UnparseInts(out, config.soc_mmio_phys, config.uart_mmio_phys, config.irq);
}

}  // namespace uart
