// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_fuzzer as fuzz, fuchsia_fuzzctl::constants::*};

/// Add defaults values to an `Options` struct.
pub fn add_defaults(options: &mut fuzz::Options) {
    options.runs = options.runs.or(Some(0));
    options.max_total_time = options.max_total_time.or(Some(0));
    options.seed = options.seed.or(Some(0));
    options.max_input_size = options.max_input_size.or(Some(1 * BYTES_PER_MB));
    options.mutation_depth = options.mutation_depth.or(Some(5));
    options.dictionary_level = options.dictionary_level.or(Some(0));
    options.detect_exits = options.detect_exits.or(Some(false));
    options.detect_leaks = options.detect_leaks.or(Some(false));
    options.run_limit = options.run_limit.or(Some(20 * NANOS_PER_MINUTE));
    options.malloc_limit = options.malloc_limit.or(Some(2 * BYTES_PER_GB));
    options.oom_limit = options.oom_limit.or(Some(2 * BYTES_PER_GB));
    options.purge_interval = options.purge_interval.or(Some(1 * NANOS_PER_SECOND));
    options.malloc_exitcode = options.malloc_exitcode.or(Some(2000));
    options.death_exitcode = options.death_exitcode.or(Some(2001));
    options.leak_exitcode = options.leak_exitcode.or(Some(2002));
    options.oom_exitcode = options.oom_exitcode.or(Some(2003));
    options.pulse_interval = options.pulse_interval.or(Some(20 * NANOS_PER_SECOND));
    options.debug = options.debug.or(Some(false));
    options.print_final_stats = options.print_final_stats.or(Some(false));
    options.use_value_profile = options.use_value_profile.or(Some(false));
    if options.sanitizer_options.is_none() {
        options.sanitizer_options =
            Some(fuzz::SanitizerOptions { name: String::default(), value: String::default() });
    }
}
