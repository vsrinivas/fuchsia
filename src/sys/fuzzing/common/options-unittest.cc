// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/options.h"

#include <gtest/gtest.h>

namespace fuzzing {
namespace {

TEST(OptionsTest, Defaults) {
  Options options;
  EXPECT_FALSE(options.has_runs());
  EXPECT_FALSE(options.has_max_total_time());
  EXPECT_FALSE(options.has_seed());
  EXPECT_FALSE(options.has_max_input_size());
  EXPECT_FALSE(options.has_mutation_depth());
  EXPECT_FALSE(options.has_dictionary_level());
  EXPECT_FALSE(options.has_detect_exits());
  EXPECT_FALSE(options.has_detect_leaks());
  EXPECT_FALSE(options.has_run_limit());
  EXPECT_FALSE(options.has_malloc_limit());
  EXPECT_FALSE(options.has_oom_limit());
  EXPECT_FALSE(options.has_purge_interval());
  EXPECT_FALSE(options.has_malloc_exitcode());
  EXPECT_FALSE(options.has_death_exitcode());
  EXPECT_FALSE(options.has_leak_exitcode());
  EXPECT_FALSE(options.has_oom_exitcode());
  EXPECT_FALSE(options.has_pulse_interval());

  AddDefaults(&options);
  EXPECT_EQ(options.runs(), kDefaultRuns);
  EXPECT_EQ(options.max_total_time(), kDefaultMaxTotalTime.get());
  EXPECT_EQ(options.seed(), kDefaultSeed);
  EXPECT_EQ(options.max_input_size(), kDefaultMaxInputSize);
  EXPECT_EQ(options.mutation_depth(), kDefaultMutationDepth);
  EXPECT_EQ(options.dictionary_level(), kDefaultDictionaryLevel);
  EXPECT_EQ(options.detect_exits(), kDefaultDetectExits);
  EXPECT_EQ(options.detect_leaks(), kDefaultDetectLeaks);
  EXPECT_EQ(options.run_limit(), kDefaultRunLimit.get());
  EXPECT_EQ(options.malloc_limit(), kDefaultMallocLimit);
  EXPECT_EQ(options.oom_limit(), kDefaultOOMLimit);
  EXPECT_EQ(options.purge_interval(), kDefaultPurgeInterval.get());
  EXPECT_EQ(options.malloc_exitcode(), kDefaultMallocExitCode);
  EXPECT_EQ(options.death_exitcode(), kDefaultDeathExitCode);
  EXPECT_EQ(options.leak_exitcode(), kDefaultLeakExitCode);
  EXPECT_EQ(options.oom_exitcode(), kDefaultOOMExitCode);
  EXPECT_EQ(options.pulse_interval(), kDefaultPulseInterval.get());
}

TEST(OptionsTest, SetAndAddDefaults) {
  Options options;
  uint32_t runs = 1000;
  zx::duration max_total_time = zx::sec(300);
  uint32_t seed = 42;
  uint64_t max_input_size = 1ULL << 10;
  uint16_t mutation_depth = 8;
  uint16_t dictionary_level = 2;
  bool detect_exits = true;
  bool detect_leaks = false;
  zx::duration run_limit = zx::sec(20);
  uint64_t malloc_limit = 64ULL << 10;
  uint64_t oom_limit = 1ULL << 20;
  zx::duration purge_interval = zx::sec(10);
  int32_t malloc_exitcode = 1000;
  int32_t death_exitcode = 1001;
  int32_t leak_exitcode = 1002;
  int32_t oom_exitcode = 1003;
  zx::duration pulse_interval = zx::sec(3);

  options.set_runs(runs);
  options.set_max_total_time(max_total_time.get());
  options.set_seed(seed);
  options.set_max_input_size(max_input_size);
  options.set_mutation_depth(mutation_depth);
  options.set_dictionary_level(dictionary_level);
  options.set_detect_exits(detect_exits);
  options.set_detect_leaks(detect_leaks);
  options.set_run_limit(run_limit.get());
  options.set_malloc_limit(malloc_limit);
  options.set_oom_limit(oom_limit);
  options.set_purge_interval(purge_interval.get());
  options.set_malloc_exitcode(malloc_exitcode);
  options.set_death_exitcode(death_exitcode);
  options.set_leak_exitcode(leak_exitcode);
  options.set_oom_exitcode(oom_exitcode);
  options.set_pulse_interval(pulse_interval.get());

  AddDefaults(&options);
  EXPECT_EQ(options.runs(), runs);
  EXPECT_EQ(options.max_total_time(), max_total_time.get());
  EXPECT_EQ(options.seed(), seed);
  EXPECT_EQ(options.max_input_size(), max_input_size);
  EXPECT_EQ(options.mutation_depth(), mutation_depth);
  EXPECT_EQ(options.dictionary_level(), dictionary_level);
  EXPECT_EQ(options.detect_exits(), detect_exits);
  EXPECT_EQ(options.detect_leaks(), detect_leaks);
  EXPECT_EQ(options.run_limit(), run_limit.get());
  EXPECT_EQ(options.malloc_limit(), malloc_limit);
  EXPECT_EQ(options.oom_limit(), oom_limit);
  EXPECT_EQ(options.purge_interval(), purge_interval.get());
  EXPECT_EQ(options.malloc_exitcode(), malloc_exitcode);
  EXPECT_EQ(options.death_exitcode(), death_exitcode);
  EXPECT_EQ(options.leak_exitcode(), leak_exitcode);
  EXPECT_EQ(options.oom_exitcode(), oom_exitcode);
  EXPECT_EQ(options.pulse_interval(), pulse_interval.get());
}

TEST(OptionsTest, Copy) {
  Options options1;
  uint32_t runs = 1000;
  uint32_t seed = 42;
  uint16_t mutation_depth = 8;
  bool detect_leaks = false;
  uint64_t malloc_limit = 64ULL << 10;
  uint32_t purge_interval = 10;
  int32_t death_exitcode = 1001;
  int32_t oom_exitcode = 1003;

  options1.set_runs(runs);
  options1.set_seed(seed);
  options1.set_mutation_depth(mutation_depth);
  options1.set_detect_leaks(detect_leaks);
  options1.set_malloc_limit(malloc_limit);
  options1.set_purge_interval(purge_interval);
  options1.set_death_exitcode(death_exitcode);
  options1.set_oom_exitcode(oom_exitcode);

  auto options2 = CopyOptions(options1);
  EXPECT_EQ(options2.runs(), runs);
  EXPECT_FALSE(options2.has_max_total_time());
  EXPECT_EQ(options2.seed(), seed);
  EXPECT_FALSE(options2.has_max_input_size());
  EXPECT_EQ(options2.mutation_depth(), mutation_depth);
  EXPECT_FALSE(options2.has_dictionary_level());
  EXPECT_FALSE(options2.has_detect_exits());
  EXPECT_EQ(options2.detect_leaks(), detect_leaks);
  EXPECT_FALSE(options2.has_run_limit());
  EXPECT_EQ(options2.malloc_limit(), malloc_limit);
  EXPECT_FALSE(options2.has_oom_limit());
  EXPECT_EQ(options2.purge_interval(), purge_interval);
  EXPECT_FALSE(options2.has_malloc_exitcode());
  EXPECT_EQ(options2.death_exitcode(), death_exitcode);
  EXPECT_FALSE(options2.has_leak_exitcode());
  EXPECT_EQ(options2.oom_exitcode(), oom_exitcode);
  EXPECT_FALSE(options2.has_pulse_interval());
}

}  // namespace
}  // namespace fuzzing
