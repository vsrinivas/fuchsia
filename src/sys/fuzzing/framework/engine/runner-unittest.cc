// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/runner.h"

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/framework/engine/runner-test.h"

namespace fuzzing {
namespace {

TEST_F(RunnerImplTest, AddDefaults) {
  RunnerImpl runner;
  Options options;
  runner.AddDefaults(&options);
  EXPECT_EQ(options.runs(), kDefaultRuns);
  EXPECT_EQ(options.max_total_time(), kDefaultMaxTotalTime);
  EXPECT_EQ(options.seed(), kDefaultSeed);
  EXPECT_EQ(options.max_input_size(), kDefaultMaxInputSize);
  EXPECT_EQ(options.mutation_depth(), kDefaultMutationDepth);
  EXPECT_EQ(options.dictionary_level(), kDefaultDictionaryLevel);
  EXPECT_EQ(options.detect_exits(), kDefaultDetectExits);
  EXPECT_EQ(options.detect_leaks(), kDefaultDetectLeaks);
  EXPECT_EQ(options.run_limit(), kDefaultRunLimit);
  EXPECT_EQ(options.malloc_exitcode(), kDefaultMallocExitcode);
  EXPECT_EQ(options.death_exitcode(), kDefaultDeathExitcode);
  EXPECT_EQ(options.leak_exitcode(), kDefaultLeakExitcode);
  EXPECT_EQ(options.oom_exitcode(), kDefaultOomExitcode);
  EXPECT_EQ(options.pulse_interval(), kDefaultPulseInterval);
}

TEST_F(RunnerImplTest, LoadCorpus) {
  RunnerImpl runner;
  // In a real fuzzer, the parameters would be supplied by the 'program.args' from the adapter's
  // component manifest.
  //
  // See also:
  //   //src/sys/fuzzing/framework/testing/data/BUILD.gn
  SetAdapterParameters(std::vector<std::string>({"data/corpus", "--ignored"}));
  Configure(&runner, RunnerTest::DefaultOptions(&runner));
  // Results are sorted.
  EXPECT_EQ(runner.ReadFromCorpus(CorpusType::SEED, 1), Input("bar"));
  EXPECT_EQ(runner.ReadFromCorpus(CorpusType::SEED, 2), Input("foo"));
}

#define RUNNER_TYPE RunnerImpl
#define RUNNER_TEST RunnerImplTest
#include "src/sys/fuzzing/common/runner-unittest.inc"
#undef RUNNER_TYPE
#undef RUNNER_TEST

TEST_F(RunnerImplTest, MergeSeedError) {
  RunnerImpl runner;
  MergeSeedError(&runner, /* expected */ ZX_ERR_INVALID_ARGS);
}

TEST_F(RunnerImplTest, Merge) {
  RunnerImpl runner;
  Merge(&runner, /* keep_errors= */ true);
}

}  // namespace
}  // namespace fuzzing
