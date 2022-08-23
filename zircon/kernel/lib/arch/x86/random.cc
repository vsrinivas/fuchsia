// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <lib/arch/random.h>
#include <lib/arch/x86/boot-cpuid.h>

namespace arch {
namespace {

template <bool Reseed>
std::optional<uint64_t> UseIntrinsic() {
  return std::nullopt;
}

#ifdef __x86_64__

template <>
[[gnu::target("rdrnd")]] std::optional<uint64_t> UseIntrinsic<true>() {
  unsigned long long int value;
  return _rdrand64_step(&value) ? std::make_optional(value) : std::nullopt;
}

template <>
[[gnu::target("rdseed")]] std::optional<uint64_t> UseIntrinsic<false>() {
  unsigned long long int value;
  return _rdseed64_step(&value) ? std::make_optional(value) : std::nullopt;
}

#endif  // __x86_64__

}  // namespace

template <bool Reseed>
bool Random<Reseed>::Supported() {
#ifdef __x86_64__
  if constexpr (Reseed) {
    return BootCpuid<CpuidExtendedFeatureFlagsB>().rdseed();
  } else {
    return BootCpuid<CpuidFeatureFlagsC>().rdrand();
  }
#endif
  return false;
}

template <bool Reseed>
std::optional<uint64_t> Random<Reseed>::Get() {
  return UseIntrinsic<Reseed>();
}

template struct Random<false>;
template struct Random<true>;

}  // namespace arch
