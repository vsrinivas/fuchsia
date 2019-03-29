// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/testing/benchmarking/benchmarking.h"

#include <algorithm>
#include <fstream>

#include <lib/fdio/spawn.h>
#include <lib/zx/handle.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/files/file.h"

namespace benchmarking {

namespace {

// Equivalent to shell expression:
//   $ rm -rf $file
void RemoveRecursive(const std::string& file) {
  int status = Spawn({"/bin/rm", "-rf", file});
  FXL_CHECK(status == 0);
}

// Equivalent to shell expression:
//   $ touch $file
void Touch(const std::string& file) {
  int status = Spawn({"/bin/touch", file});
  FXL_CHECK(status == 0);
}

int CatapultConvert(const std::string& input, const std::string& output,
                    const std::vector<std::string>& catapult_converter_args) {
  std::vector<std::string> command = {
      "/pkgfs/packages/catapult_converter/0/bin/catapult_converter", "--input",
      input, "--output", output};
  std::copy(catapult_converter_args.begin(), catapult_converter_args.end(),
            std::back_inserter(command));
  return Spawn(command);
}

// Return the basename of |path|.
// Examples:
//   "foo/bar" -> "bar"
//   "foo/bar/baz" -> "baz"
std::string Basename(const std::string& path) {
  auto split =
      fxl::SplitStringCopy(path, "/", fxl::kKeepWhitespace, fxl::kSplitWantAll);
  FXL_CHECK(!split.empty());
  return split.back();
}

// Join |paths| into one path.
std::string JoinPaths(const std::vector<std::string>& paths) {
  return fxl::JoinStrings(paths, "/");
}

}  // namespace

// static
std::optional<BenchmarksRunner> BenchmarksRunner::Create(int argc,
                                                         const char** argv) {
  if (argc < 3 || std::string(argv[2]) != "--catapult-converter-args") {
    FXL_LOG(ERROR) << "Error: Missing '--catapult-converter-args' argument";
    FXL_LOG(ERROR) << "Usage: " << argv[0]
                   << " <output-dir> --catapult-converter-args <args>";
    return {};
  }

  BenchmarksRunner benchmarks_runner;
  benchmarks_runner.out_dir_ = argv[1];
  for (int i = 3; i < argc; i++) {
    benchmarks_runner.catapult_converter_args_.push_back(argv[i]);
    // TODO(PT-73): Consider using an arguments parsing library here instead.
    if (std::string(argv[i]) == "--bots") {
      FXL_CHECK(i + 1 < argc);
      benchmarks_runner.benchmarks_bot_name_ = argv[i + 1];
    }
  }

  return benchmarks_runner;
}

void BenchmarksRunner::AddTspecBenchmark(const std::string& name,
                                         const std::string& tspec_file,
                                         const std::string& test_suite) {
  std::string out_file = JoinPaths({out_dir_, name + ".json"});
  std::vector<std::string> command = {"/bin/trace", "record",
                                      "--spec-file=" + tspec_file,
                                      "--benchmark-results-file=" + out_file};
  if (!test_suite.empty()) {
    command.push_back("--test-suite=" + test_suite);
  }
  AddCustomBenchmark(name, command, out_file);
}

void BenchmarksRunner::AddLibPerfTestBenchmark(
    const std::string& name, const std::string& libperftest_binary,
    const std::vector<std::string>& extra_args) {
  std::string out_file = JoinPaths({out_dir_, name + ".json"});
  std::vector<std::string> command = {libperftest_binary, "-p",
                                      "--out=" + out_file};
  std::copy(extra_args.begin(), extra_args.end(), std::back_inserter(command));
  AddCustomBenchmark(name, command, out_file);
}

void BenchmarksRunner::AddCustomBenchmark(
    const std::string& name, const std::vector<std::string>& command,
    const std::string& results_file) {
  tasks_.push_back([=]() {
    RemoveRecursive(results_file);
    Touch(results_file);
    auto command_as_string = fxl::JoinStrings(command, " ");
    FXL_LOG(INFO) << "Running \"" << command_as_string << '"';

    int command_status = Spawn(command);
    FXL_CHECK(command_status == 0)
        << "Non-zero exit status " << command_status << " from running \""
        << command_as_string << '"';

    if (!files::IsFile(results_file)) {
      FXL_LOG(ERROR) << "Expected file " << results_file
                     << " to exist, and it did not.";
      got_errors_ = true;
      WriteSummaryEntry(name, results_file, SummaryEntryResult::kFail);
      return;
    }

    std::string catapult_file = results_file + ".catapult_json";
    int catapult_convert_status =
        CatapultConvert(results_file, catapult_file, catapult_converter_args_);
    if (catapult_convert_status != 0) {
      FXL_LOG(ERROR) << "Failed to run catapult_converter";
      WriteSummaryEntry(name, results_file, SummaryEntryResult::kFail);
      return;
    }

    WriteSummaryEntry(name, results_file, SummaryEntryResult::kPass);
    WriteSummaryEntry(name + ".catapult_json", catapult_file,
                      SummaryEntryResult::kPass);
  });
}

void BenchmarksRunner::AddTask(std::function<void()> task) {
  tasks_.push_back(task);
}

void BenchmarksRunner::Finish() {
  while (!tasks_.empty()) {
    auto task = std::move(tasks_.front());
    tasks_.pop_front();
    task();
  }

  {
    std::string summary_filepath = JoinPaths({out_dir_, "summary.json"});
    std::string summary = fxl::Substitute(R"(
{
  "tests": [$0]
}
)",
                                          benchmark_summaries_);
    FXL_LOG(INFO) << "writing summary.json to " << summary_filepath;
    std::ofstream ofs(summary_filepath);
    FXL_CHECK(ofs.good()) << "Failed to open " << summary_filepath
                          << " for writing";
    ofs << summary;
    ofs.flush();
    FXL_CHECK(ofs.good()) << "Failed to write to " << summary_filepath;
  }

  if (got_errors_) {
    exit(1);
  }
}

std::string BenchmarksRunner::MakeTempFile() {
  return JoinPaths({out_dir_, "benchmarking_temp_file_" +
                                  std::to_string(next_temp_file_index_++)});
}

void BenchmarksRunner::WriteSummaryEntry(const std::string& name,
                                         const std::string& results_file,
                                         SummaryEntryResult result) {
  // Map |result| to a string defined at
  // https://fuchsia.googlesource.com/infra/recipes/+/08669b6c97a6f4d73a65d5cd1f23ca8dd7b167cb/recipe_modules/fuchsia/api.py#118.
  std::string result_string = [result]() {
    if (result == SummaryEntryResult::kPass) {
      return "PASS";
    } else if (result == SummaryEntryResult::kFail) {
      return "FAIL";
    } else {
      FXL_CHECK(false);
      return "FAIL";
    }
  }();

  std::string results_filename = Basename(results_file);
  std::string entry = fxl::Substitute(R"(
{
  "name": "$0",
  "output_file": "$1",
  "result": "$2"
}
)",
                                      name, results_filename, result_string);

  benchmark_summaries_ += (benchmark_summaries_.empty() ? "" : ",") + entry;
}

int Spawn(const std::vector<std::string>& command) {
  std::vector<const char*> raw_command;
  for (const auto& arg : command) {
    raw_command.push_back(arg.c_str());
  }
  raw_command.push_back(nullptr);

  zx::handle subprocess;
  zx_status_t status =
      fdio_spawn(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, raw_command[0],
                 raw_command.data(), subprocess.reset_and_get_address());
  FXL_CHECK(status == ZX_OK);

  zx_signals_t signals_observed = 0;
  status = subprocess.wait_one(ZX_TASK_TERMINATED, zx::time(ZX_TIME_INFINITE),
                               &signals_observed);
  FXL_CHECK(status == ZX_OK);
  zx_info_process_t proc_info;
  status = subprocess.get_info(ZX_INFO_PROCESS, &proc_info, sizeof(proc_info),
                               nullptr, nullptr);
  FXL_CHECK(status == ZX_OK);
  return proc_info.return_code;
}

}  // namespace benchmarking
