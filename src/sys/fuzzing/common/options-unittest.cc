// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/options.h"

#include <gtest/gtest.h>

namespace fuzzing {
namespace {

TEST(OptionsTest, Set) {
  // To ensure this test doesn't miss any options, it defines variables with the same name as the
  // "snake_case" column in options.inc and then uses the `FUCHSIA_FUZZER_OPTIONS` macro.
  uint32_t runs = 1000;
  int64_t max_total_time = zx::sec(300).get();
  uint32_t seed = 42;
  uint64_t max_input_size = 1ULL << 10;
  uint16_t mutation_depth = 8;
  uint16_t dictionary_level = 2;
  bool detect_exits = true;
  bool detect_leaks = false;
  int64_t run_limit = zx::sec(20).get();
  uint64_t malloc_limit = 64ULL << 10;
  uint64_t oom_limit = 1ULL << 20;
  int64_t purge_interval = zx::sec(10).get();
  int32_t malloc_exitcode = 1000;
  int32_t death_exitcode = 1001;
  int32_t leak_exitcode = 1002;
  int32_t oom_exitcode = 1003;
  int64_t pulse_interval = zx::sec(3).get();
  bool debug = true;
  bool print_final_stats = true;
  bool use_value_profile = true;
  SanitizerOptions sanitizer_options = {.name = "MYSAN_OPTIONS", .value = "key1=val1:key2=val2"};

  Options options1;
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) options1.set_##option(option);
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION

  Options options2;
  SetOptions(&options2, options1);
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  EXPECT_EQ(options2.option(), option);
#include "src/sys/fuzzing/common/options-literal.inc"
#undef FUCHSIA_FUZZER_OPTION

  EXPECT_EQ(options2.sanitizer_options().name, sanitizer_options.name);
  EXPECT_EQ(options2.sanitizer_options().value, sanitizer_options.value);

  // Special case: sanitizer_options.name must end in "...SAN_OPTIONS" or it is ignored.
  Options invalid;
  SanitizerOptions bad_options = {.name = "BAD_OPTIONS", .value = "key3=val3:key4=val4"};
  invalid.set_sanitizer_options(bad_options);

  SetOptions(&options2, invalid);
  EXPECT_EQ(options2.sanitizer_options().name, sanitizer_options.name);
  EXPECT_EQ(options2.sanitizer_options().value, sanitizer_options.value);
}

TEST(OptionsTest, Copy) {
  // Start with default values.
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) type option = default_value;
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION

  // Change some of the values, and set those.
  runs = 1000;
  seed = 42;
  mutation_depth = 8;
  detect_leaks = false;
  malloc_limit = 64ULL << 10;
  purge_interval = 10;
  death_exitcode = 1001;
  oom_exitcode = 1003;
  debug = true;
  sanitizer_options = {.name = "MYSAN_OPTIONS", .value = "key1=val1:key2=val2"};

  Options options1;
  options1.set_runs(runs);
  options1.set_seed(seed);
  options1.set_mutation_depth(mutation_depth);
  options1.set_detect_leaks(detect_leaks);
  options1.set_malloc_limit(malloc_limit);
  options1.set_purge_interval(purge_interval);
  options1.set_death_exitcode(death_exitcode);
  options1.set_oom_exitcode(oom_exitcode);
  options1.set_debug(debug);
  options1.set_sanitizer_options(sanitizer_options);

  // Copy, and verify the set values are copied and the missing values are defaulted.
  auto options2 = CopyOptions(options1);
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  EXPECT_EQ(options2.option(), option);
#include "src/sys/fuzzing/common/options-literal.inc"
#undef FUCHSIA_FUZZER_OPTION

  EXPECT_EQ(options2.sanitizer_options().name, sanitizer_options.name);
  EXPECT_EQ(options2.sanitizer_options().value, sanitizer_options.value);
}

TEST(OptionsTest, AddDefaults) {
  // |AddDefaults| should add anything that is missing...
  Options options1;
  AddDefaults(&options1);
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  EXPECT_EQ(options1.option(), kDefault##Option);
#include "src/sys/fuzzing/common/options-literal.inc"
#undef FUCHSIA_FUZZER_OPTION

  EXPECT_TRUE(options1.sanitizer_options().name.empty());
  EXPECT_TRUE(options1.sanitizer_options().value.empty());

  // ...but it should not overwrite anything already there.
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) type option = default_value;
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION

  runs = 1000;
  seed = 42;
  mutation_depth = 8;
  detect_leaks = false;
  malloc_limit = 64ULL << 10;
  purge_interval = 10;
  death_exitcode = 1001;
  oom_exitcode = 1003;
  debug = true;
  sanitizer_options = {.name = "MYSAN_OPTIONS", .value = "key1=val1:key2=val2"};

  Options options2;
  options2.set_runs(runs);
  options2.set_seed(seed);
  options2.set_mutation_depth(mutation_depth);
  options2.set_detect_leaks(detect_leaks);
  options2.set_malloc_limit(malloc_limit);
  options2.set_purge_interval(purge_interval);
  options2.set_death_exitcode(death_exitcode);
  options2.set_oom_exitcode(oom_exitcode);
  options2.set_debug(debug);
  options2.set_sanitizer_options(sanitizer_options);

  AddDefaults(&options2);
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  EXPECT_EQ(options2.option(), option);
#include "src/sys/fuzzing/common/options-literal.inc"
#undef FUCHSIA_FUZZER_OPTION

  EXPECT_EQ(options2.sanitizer_options().name, sanitizer_options.name);
  EXPECT_EQ(options2.sanitizer_options().value, sanitizer_options.value);
}

}  // namespace
}  // namespace fuzzing
