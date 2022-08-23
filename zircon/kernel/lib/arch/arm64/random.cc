// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/arm64/feature.h>
#include <lib/arch/intrin.h>
#include <lib/arch/random.h>

namespace arch {
namespace {

// TODO(fxbug.dev/102847): GCC's <arm_acle.h> does have these, but we can't use
// that header with -mgeneral-regs.
#ifndef __clang__
inline int __rndr(uint64_t* ptr) { return __builtin_aarch64_rndr(ptr); }
inline int __rndrrs(uint64_t* ptr) { return __builtin_aarch64_rndrrs(ptr); }
#endif

}  // namespace

template <bool Reseed>
bool Random<Reseed>::Supported() {
  return ArmIdAa64IsaR0El1::Read().rndr() != ArmIdAa64IsaR0El1::Rndr::kNone;
}

template <bool Reseed>
std::optional<uint64_t> Random<Reseed>::Get() {
  constexpr auto intrinsic = Reseed ? __rndrrs : __rndr;

  uint64_t value;
  if (intrinsic(&value) == 0) {
    return value;
  }

  return std::nullopt;
}

template struct Random<false>;
template struct Random<true>;

}  // namespace arch
