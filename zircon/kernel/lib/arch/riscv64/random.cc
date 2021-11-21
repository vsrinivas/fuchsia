// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <lib/arch/random.h>

namespace arch {

template <bool Reseed>
bool Random<Reseed>::Supported() {
  return false;
}

template <bool Reseed>
std::optional<uint64_t> Random<Reseed>::Get() {
  return std::nullopt;
}

template struct Random<false>;
template struct Random<true>;

}  // namespace arch
