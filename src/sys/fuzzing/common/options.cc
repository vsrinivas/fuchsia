// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

void AddDefaults(Options* options) {
  if (!options->has_runs()) {
    options->set_runs(kDefaultRuns);
  }
  if (!options->has_max_total_time()) {
    options->set_max_total_time(kDefaultMaxTotalTime.get());
  }
  if (!options->has_seed()) {
    options->set_seed(kDefaultSeed);
  }
  if (!options->has_max_input_size()) {
    options->set_max_input_size(kDefaultMaxInputSize);
  }
  if (!options->has_mutation_depth()) {
    options->set_mutation_depth(kDefaultMutationDepth);
  }
  if (!options->has_dictionary_level()) {
    options->set_dictionary_level(kDefaultDictionaryLevel);
  }
  if (!options->has_detect_exits()) {
    options->set_detect_exits(kDefaultDetectExits);
  }
  if (!options->has_detect_leaks()) {
    options->set_detect_leaks(kDefaultDetectLeaks);
  }
  if (!options->has_run_limit()) {
    options->set_run_limit(kDefaultRunLimit.get());
  }
  if (!options->has_malloc_limit()) {
    options->set_malloc_limit(kDefaultMallocLimit);
  }
  if (!options->has_oom_limit()) {
    options->set_oom_limit(kDefaultOOMLimit);
  }
  if (!options->has_purge_interval()) {
    options->set_purge_interval(kDefaultPurgeInterval.get());
  }
  if (!options->has_malloc_exitcode()) {
    options->set_malloc_exitcode(kDefaultMallocExitCode);
  }
  if (!options->has_death_exitcode()) {
    options->set_death_exitcode(kDefaultDeathExitCode);
  }
  if (!options->has_leak_exitcode()) {
    options->set_leak_exitcode(kDefaultLeakExitCode);
  }
  if (!options->has_oom_exitcode()) {
    options->set_oom_exitcode(kDefaultOOMExitCode);
  }
  if (!options->has_pulse_interval()) {
    options->set_pulse_interval(kDefaultPulseInterval.get());
  }
}

std::shared_ptr<Options> DefaultOptions() {
  auto options = std::make_shared<Options>();
  AddDefaults(options.get());
  return options;
}

Options CopyOptions(const Options& options) {
  Options copy;
  if (options.has_runs()) {
    copy.set_runs(options.runs());
  }
  if (options.has_max_total_time()) {
    copy.set_max_total_time(options.max_total_time());
  }
  if (options.has_seed()) {
    copy.set_seed(options.seed());
  }
  if (options.has_max_input_size()) {
    copy.set_max_input_size(options.max_input_size());
  }
  if (options.has_mutation_depth()) {
    copy.set_mutation_depth(options.mutation_depth());
  }
  if (options.has_dictionary_level()) {
    copy.set_dictionary_level(options.dictionary_level());
  }
  if (options.has_detect_exits()) {
    copy.set_detect_exits(options.detect_exits());
  }
  if (options.has_detect_leaks()) {
    copy.set_detect_leaks(options.detect_leaks());
  }
  if (options.has_run_limit()) {
    copy.set_run_limit(options.run_limit());
  }
  if (options.has_malloc_limit()) {
    copy.set_malloc_limit(options.malloc_limit());
  }
  if (options.has_oom_limit()) {
    copy.set_oom_limit(options.oom_limit());
  }
  if (options.has_purge_interval()) {
    copy.set_purge_interval(options.purge_interval());
  }
  if (options.has_malloc_exitcode()) {
    copy.set_malloc_exitcode(options.malloc_exitcode());
  }
  if (options.has_death_exitcode()) {
    copy.set_death_exitcode(options.death_exitcode());
  }
  if (options.has_leak_exitcode()) {
    copy.set_leak_exitcode(options.leak_exitcode());
  }
  if (options.has_oom_exitcode()) {
    copy.set_oom_exitcode(options.oom_exitcode());
  }
  if (options.has_pulse_interval()) {
    copy.set_pulse_interval(options.pulse_interval());
  }
  return copy;
}

}  // namespace fuzzing
