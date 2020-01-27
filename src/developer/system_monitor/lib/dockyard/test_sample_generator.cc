// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/lib/dockyard/test_sample_generator.h"

#include <random>

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

namespace dockyard {

namespace {

// The stride is how much time is in each sample.
constexpr SampleTimeNs CalcStride(SampleTimeNs start, SampleTimeNs finish,
                                  size_t count) {
  SampleTimeNs stride = (finish - start);
  if (count) {
    stride /= count;
  }
  return stride;
}

// Generate a random value from low to high (inclusive).
SampleValue GenValue(SampleValue low, SampleValue high) {
  if (low == high) {
    return low;
  }
  return rand() % (high - low) + low;
}

}  // namespace

void GenerateRandomSamples(const RandomSampleGenerator& gen,
                           Dockyard* dockyard) {
  srand(gen.seed);
  constexpr double PI_DIV_16 = 3.141592653 / 16.0;
  SampleTimeNs time_range = gen.finish - gen.start;
  SampleTimeNs time_stride =
      CalcStride(gen.start, gen.finish, gen.sample_count);
  SampleValue value_range = gen.value_max - gen.value_min;
  SampleValue value_quarter = (gen.value_max - gen.value_min) / 4;
  SampleTimeNs time = gen.start;
  SampleValue value = gen.value_min;
  for (size_t sample_n = 0; sample_n < gen.sample_count; ++sample_n) {
    switch (gen.value_style) {
      case RandomSampleGenerator::VALUE_STYLE_MONO_INCREASE:
        value = gen.value_min + value_range * sample_n / gen.sample_count;
        break;
      case RandomSampleGenerator::VALUE_STYLE_MONO_DECREASE:
        value = gen.value_max - value_range * sample_n / gen.sample_count;
        break;
      case RandomSampleGenerator::VALUE_STYLE_JAGGED:
        value = ((sample_n % 2)
                     ? GenValue(gen.value_min, gen.value_min + value_quarter)
                     : GenValue(gen.value_max - value_quarter, gen.value_max));
        break;
      case RandomSampleGenerator::VALUE_STYLE_RANDOM:
        value = GenValue(gen.value_min, gen.value_max);
        break;
      case RandomSampleGenerator::VALUE_STYLE_RANDOM_WALK:
        value += GenValue(0, value_quarter) - value_quarter / 2;
        if (value < gen.value_min) {
          value = gen.value_min;
        }
        if (value > gen.value_max) {
          value = gen.value_max;
        }
        break;
      case RandomSampleGenerator::VALUE_STYLE_SINE_WAVE:
        value =
            gen.value_min + value_range * (1 + sin(PI_DIV_16 * sample_n)) / 2;
        break;
    }
    dockyard->AddSample(gen.dockyard_id, Sample(time, value));
    // Make sure time advances by at least one nanosecond.
    ++time;
    switch (gen.time_style) {
      case RandomSampleGenerator::TIME_STYLE_LINEAR:
        time = gen.start + time_range * (sample_n + 1) / gen.sample_count;
        break;
      case RandomSampleGenerator::TIME_STYLE_SHORT_STAGGER:
        time += GenValue(time_stride * 0.5, time_stride * 1.5);
        break;
      case RandomSampleGenerator::TIME_STYLE_LONG_STAGGER:
        time += GenValue(0, time_stride * 2);
        break;
      case RandomSampleGenerator::TIME_STYLE_CLUMPED:
        time += ((sample_n % 4) ? GenValue(0, time_stride / 4)
                                : time_stride * 2.25);
        break;
      case RandomSampleGenerator::TIME_STYLE_OPEN:
        time += GenValue(0, time_stride * 2);
        break;
    }
  }
}

}  // namespace dockyard
