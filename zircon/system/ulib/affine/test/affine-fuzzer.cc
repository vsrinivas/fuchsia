// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/affine/ratio.h>
#include <lib/affine/transform.h>
#include <stdint.h>

#include <fuzzer/FuzzedDataProvider.h>

using affine::Ratio;
using affine::Transform;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);

  // Note: This intentionally skips trivial methods like Ratio::Inverse()

  // Construct two Ratios
  Ratio ratios[2];
  for (auto i = 0; i < 2; i++) {
    auto numerator = data_provider.ConsumeIntegral<uint32_t>();
    // For denominators, we want to avoid expected errors for zero values
    auto denominator = data_provider.ConsumeIntegralInRange<uint32_t>(1, UINT32_MAX);
    ratios[i] = Ratio{numerator, denominator};
  }

  // Call Reduce() on copies, because it mutates in-place
  Ratio(ratios[0].numerator(), ratios[0].denominator()).Reduce();
  Ratio(ratios[1].numerator(), ratios[1].denominator()).Reduce();

  // Product() defaults to Exact::Yes which would assert on loss of precision
  Ratio::Product(ratios[0], ratios[1], Ratio::Exact::No);
  Ratio::Product(ratios[1], ratios[0], Ratio::Exact::No);

  // Exercise Ratio::Scale()
  auto n = data_provider.ConsumeIntegral<int64_t>();
  ratios[0] * n;
  ratios[1] * n;

  // Construct two Transforms
  Transform transforms[2];
  for (auto i = 0; i < 2; i++) {
    auto a_offset = data_provider.ConsumeIntegral<int64_t>();
    auto b_offset = data_provider.ConsumeIntegral<int64_t>();
    transforms[i] = Transform{a_offset, b_offset, ratios[i]};
  }

  // We only use Apply() and not ApplyInverse() so we can avoid division by zero
  transforms[0].Apply(n);
  transforms[1].Apply(n);

  // Compose() defaults to Exact::Yes which would assert on loss of precision
  Transform::Compose(transforms[0], transforms[1], Ratio::Exact::No).Apply(n);
  Transform::Compose(transforms[1], transforms[0], Ratio::Exact::No).Apply(n);

  return 0;
}
