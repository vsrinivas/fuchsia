// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_TESTING_BENCHMARKING_BENCHMARKING_H_
#define GARNET_TESTING_BENCHMARKING_BENCHMARKING_H_

#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace benchmarking {

// A context in which benchmarks are run.  Expected usage is a layer/domain
// specific wrapper binary creating a |BenchmarksRunner| from command line
// arguments supplied by a buildbot recipe, adding all desired benchmarks via
// the |BenchmarksRunner::Add*| methods, and then calling
// |BenchmarksRunner::Finish|.  See the //garnet/tests/benchmarks package for
// example usage.
class BenchmarksRunner {
 public:
  // Create a new |BenchmarksRunner| using arguments supplied via buildbot.
  static std::optional<BenchmarksRunner> Create(int argc, const char** argv);

  BenchmarksRunner(const BenchmarksRunner&) = delete;
  void operator=(const BenchmarksRunner&) = delete;
  BenchmarksRunner(BenchmarksRunner&&) = default;
  BenchmarksRunner& operator=(BenchmarksRunner&&) = default;

  // Add a benchmark of name |name| that is specified by a tspec file located
  // at |tspec_file|.
  //
  // This corresponds to running something like:
  //   $ trace record \
  //       --spec-file=<tspec_file> \
  //       --benchmark-results-file=/tmp/example.json
  //       --test-suite=<test_suite> (optional)
  void AddTspecBenchmark(const std::string& name, const std::string& tspec_file,
                         const std::string& test_suite = "");

  // Add a benchmark of name |name|, specified by |libperftest_binary|, which
  // is a path to the binary that runs a libperftest benchmark.  |extra_args|
  // is a list of extra arguments (in addition to standard libperftest
  // arguments) to be passed to the libperftest binary.
  //
  // This corresponds to running something like:
  //   $ <libperf_binary> -p --out=/tmp/example.json
  //
  void AddLibPerfTestBenchmark(const std::string& name,
                               const std::string& libperftest_binary,
                               const std::vector<std::string>& extra_args = {});

  // Add a custom benchmark of name |name| that is an arbitrary command. After
  // running, |command| is expected to output a Fuchsia benchmarking output
  // file at path |results_file|.
  void AddCustomBenchmark(const std::string& name,
                          const std::vector<std::string>& command,
                          const std::string& results_file);

  // Add a custom, non-benchmark task to be executed by the
  // |BenchmarksRunner|.  For example, one might want to dump additional debug
  // information in between benchmarks, such as listing block devices.
  void AddTask(std::function<void()> task);

  // Run all benchmarks and tasks that were previously added.  After that,
  // produce a summary file of benchmarks that were run, which lists all of
  // the benchmarks that ran, along with their results.
  void Finish();

  // Create a new temporary file path.  This is intended to be used by custom
  // benchmarks (see |AddCustomBenchmark|), which need to specify an output
  // file that contains results from running the benchmark.
  std::string MakeTempFile();

  // This is currently only exposed for temporary logic in the garnet
  // filesystem benchmarks.  Please do not use this unless you really need it.
  std::string benchmarks_bot_name() const { return benchmarks_bot_name_; }

 private:
  enum class SummaryEntryResult {
    kPass,
    kFail,
  };

  BenchmarksRunner() = default;

  // Records the result of running a benchmark in the summary.
  //
  // |name|: The name of the benchmark.
  // |result_file|: Path to the results file, relative to |out_dir_|.
  // |result|: Whether the benchmark succeeded or failed.
  void WriteSummaryEntry(const std::string& name,
                         const std::string& results_file,
                         SummaryEntryResult result);

  std::deque<std::function<void()>> tasks_;
  int next_temp_file_index_ = 0;

  // A string of JSON objects representing benchmark results. The contents of
  // this string are written to a `summary.json` file after all benchmarks
  // have run.  Infra uses this file when running benchmarks on hardware as a
  // sort of manifest.  It indicates which tests ran, where their output files
  // are located, and whether a test passed or failed. Each added benchmark
  // records results to this summary.  The summary's schema is defined at:
  // https://fuchsia.googlesource.com/zircon/+/master/system/uapp/runtests/summary-schema.json
  std::string benchmark_summaries_;

  // Whether any errors occurred while running benchmarks or executing tasks.
  bool got_errors_ = false;

  std::string out_dir_;
  std::vector<std::string> catapult_converter_args_;
  std::string benchmarks_bot_name_;
};

// Spawn and block on |command| (via |fdio_spawn|), returning its exit status.
int Spawn(const std::vector<std::string>& command);

}  // namespace benchmarking

#endif  // GARNET_TESTING_BENCHMARKING_BENCHMARKING_H_
