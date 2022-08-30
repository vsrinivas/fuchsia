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

// The `rdrand` and `rdseed` instructions might fail if the system is under
// heavy load. Intel recommends wrapping them in a limited retry-loop to
// increase the chance of success.
//
// [intel/drng] 5.2.1 Retry Recommendations
// For rdrand, a failure after 10 retries would indicate a CPU issue.
//
// [intel/drng] 5.3.1 Retry Recommendations
// For rdseed, the guideline is to retry with calls to "pause" in between, and
// give up after a short number of retries.
// It is common for rdseed to fail if it is being called faster than it can
// generate values. There are no guarantees that it will ever succeed.
template <bool Reseed>
constexpr int kRetries = Reseed ? 200 : 10;

#ifdef __x86_64__
template <>
[[gnu::target("rdrnd")]] std::optional<uint64_t> UseIntrinsic<false>() {
  unsigned long long int value;
  return _rdrand64_step(&value) ? std::make_optional(value) : std::nullopt;
}

template <>
[[gnu::target("rdseed")]] std::optional<uint64_t> UseIntrinsic<true>() {
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
std::optional<uint64_t> Random<Reseed>::Get(std::optional<unsigned int> retries) {
  unsigned int i = retries.value_or(kRetries<Reseed>);
  do {
    if (auto result = UseIntrinsic<Reseed>(); result.has_value()) {
      return result;
    }
    if constexpr (Reseed) {
      arch::Yield();
    }
  } while (i-- > 0);
  return std::nullopt;
}

template struct Random<false>;
template struct Random<true>;

}  // namespace arch
