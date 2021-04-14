// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/feature.h>
#include <lib/arch/intrin.h>
#include <lib/arch/random.h>

namespace arch {

template <bool Reseed>
bool Random<Reseed>::Supported() {
  return ArmIdAa64IsaR0El1::Read().rndr() != ArmIdAa64IsaR0El1::Rndr::kNone;
}

template <bool Reseed>
std::optional<uint64_t> Random<Reseed>::Get() {
  uint64_t value;
  uint32_t flag;

#define READ_REGISTER(regname)                 \
  __asm__ volatile(                            \
      ".arch armv8.5-a+rng\n\t"                \
      "mrs %[value], " #regname                \
      "\n\t"                                   \
      "cset %w[flag], ne"                      \
      : [value] "=r"(value), [flag] "=r"(flag) \
      :                                        \
      : "cc")

  if constexpr (Reseed) {
    READ_REGISTER(RNDRRS);
  } else {
    READ_REGISTER(RNDR);
  }

#undef READ_REGISTER

  return flag ? std::make_optional(value) : std::nullopt;
}

template struct Random<false>;
template struct Random<true>;

}  // namespace arch
