// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// To use this file, define a macro like
//
//   #define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) ...
//
// before including this file.

#ifndef FUCHSIA_FUZZER_OPTION
#error "FUCHSIA_FUZZER_OPTION must be defined before including this file."
#endif

// Keep these in sync with the documentation in the FIDL file.
// TODO(fxbug.dev/84367): Some customers have also requested a "min_input_size" option.
//                   (type,     snake_case_name,  CamelCaseName,   default-value);
FUCHSIA_FUZZER_OPTION(uint32_t, runs,             Runs,            0);
FUCHSIA_FUZZER_OPTION(int64_t,  max_total_time,   MaxTotalTime,    zx::sec(0).get());
FUCHSIA_FUZZER_OPTION(uint32_t, seed,             Seed,            0);
FUCHSIA_FUZZER_OPTION(uint64_t, max_input_size,   MaxInputSize,    1ULL << 20);
FUCHSIA_FUZZER_OPTION(uint64_t, mutation_depth,   MutationDepth,   5);
FUCHSIA_FUZZER_OPTION(uint16_t, dictionary_level, DictionaryLevel, 0);
FUCHSIA_FUZZER_OPTION(bool,     detect_exits,     DetectExits,     false);
FUCHSIA_FUZZER_OPTION(bool,     detect_leaks,     DetectLeaks,     false);
FUCHSIA_FUZZER_OPTION(int64_t,  run_limit,        RunLimit,        zx::sec(1200).get());
FUCHSIA_FUZZER_OPTION(uint64_t, malloc_limit,     MallocLimit,     2ULL << 30);
FUCHSIA_FUZZER_OPTION(uint64_t, oom_limit,        OomLimit,        2ULL << 30);
FUCHSIA_FUZZER_OPTION(int64_t,  purge_interval,   PurgeInterval,   zx::sec(1).get());
FUCHSIA_FUZZER_OPTION(int32_t,  malloc_exitcode,  MallocExitcode,  2000);
FUCHSIA_FUZZER_OPTION(int32_t,  death_exitcode,   DeathExitcode,   2001);
FUCHSIA_FUZZER_OPTION(int32_t,  leak_exitcode,    LeakExitcode,    2002);
FUCHSIA_FUZZER_OPTION(int32_t,  oom_exitcode,     OomExitcode,     2003);
FUCHSIA_FUZZER_OPTION(int64_t,  pulse_interval,   PulseInterval,   zx::sec(20).get());
