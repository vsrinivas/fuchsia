// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_TEST_SAMPLE_GENERATOR_H_
#define GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_TEST_SAMPLE_GENERATOR_H_

#include "dockyard.h"

namespace dockyard {

// Settings for calling GenerateRandomSamples() to create test samples.
struct RandomSampleGenerator {
  RandomSampleGenerator()
      : dockyard_id(0),
        seed(0),
        time_style(TIME_STYLE_LINEAR),
        start(0),
        finish(100),
        value_style(VALUE_STYLE_SINE_WAVE),
        value_min(0),
        value_max(SAMPLE_MAX_VALUE),
        sample_count(100) {}

  // How time should progress.
  enum RandomTimeStyle {
    // Add samples at the same interval, without variance.
    TIME_STYLE_LINEAR,
    // Vary times for samples by a small amount.
    TIME_STYLE_SHORT_STAGGER,
    // Like TIME_STYLE_SHORT_STAGGER, with more variance.
    TIME_STYLE_LONG_STAGGER,
    // Add clumps of samples separated by relatively long absences of samples.
    TIME_STYLE_CLUMPED,
    // Let the generator do whatever it likes.
    TIME_STYLE_OPEN,
  };

  // How values are created.
  enum RandomValueStyle {
    // Start at |min| and go to |max| without decreasing.
    VALUE_STYLE_MONO_INCREASE,
    // Start at |max| and go to |min| without increasing.
    VALUE_STYLE_MONO_DECREASE,
    // Choose random values in the upper quarter of the range, then the lower
    // quarter of the range, and so on.
    VALUE_STYLE_JAGGED,
    // Random values from |min| to |max| for each value.
    VALUE_STYLE_RANDOM,
    // Go a little up or down at each step, staying within |min| and |max|.
    VALUE_STYLE_RANDOM_WALK,
    // Plot a sine wave within |min| and |max|.
    VALUE_STYLE_SINE_WAVE,
  };

  // E.g. as provided by |GetDockyardId()| to get an ID value.
  DockyardId dockyard_id;
  // Value used for srand(). Using a consistent seed value will yield
  // predictable samples.
  unsigned int seed;

  // How time should progress.
  RandomTimeStyle time_style;
  // The initial time for this set of samples. The first sample will be created
  // at this time stamp.
  SampleTimeNs start;
  // The end time for this set of samples. This is a guide, the last sample may
  // be a bit shy or exceed this value.
  SampleTimeNs finish;

  // How values are created.
  RandomValueStyle value_style;
  // The lowest value. It's possible no generated sample will actually have this
  // value, but none will be less than |value_min|.
  SampleValue value_min;
  // The highest value. It's possible no generated sample will actually have
  // this value, but none will be higher than |value_max|.
  SampleValue value_max;

  // How many samples to create. This will overrule |finish| time. I.e. more
  // samples will be created to satisfy |sample_count| even if that results in
  // going past the |finish| time.
  size_t sample_count;
};

// Insert test samples. This is to assist in testing the GUI. Given the same
// inputs, the same samples will be generated (i.e. pseudo-random, not truly
// random).
void GenerateRandomSamples(const RandomSampleGenerator& gen,
                           Dockyard* dockyard);

}  // namespace dockyard

#endif  // GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_TEST_SAMPLE_GENERATOR_H_
