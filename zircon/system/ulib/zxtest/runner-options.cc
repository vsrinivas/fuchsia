// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <getopt.h>

#include <ctime>

#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <fbl/string_printf.h>
#include <zxtest/base/log-sink.h>
#include <zxtest/base/runner.h>

namespace zxtest {

namespace {
using Options = Runner::Options;

bool GetBoolFlag(const char* arg) {
  return arg == nullptr || strlen(arg) == 0 || strcmp("true", arg) == 0;
}

// getopt_long ignores -f value and --flag value opt args, and requires either = or -fvalue for
// optional arguments. This will return an opt arg if available.
char* GetOptArg(int opt, int argc, char** argv) {
  // We might need to check we ar enot skipping any argument.
  int index = opt;

  if (index >= argc) {
    return nullptr;
  }

  if (argv[index] == nullptr) {
    return nullptr;
  }

  if (argv[index][0] == '\0') {
    return nullptr;
  }

  if (argv[index][0] == '-') {
    return nullptr;
  }

  return argv[index];
}

constexpr char kUsageMsg[] = R"(
    [OPTIONS]
    --help[-h]                                         Prints this message.
    --gtest_filter[-f] PATTERN                         Runner will consider only registered
                                                       tests that match PATTERN.
    --gtest_list_tests[-l] BOOL                        Runner will list all registered tests
                                                       that would be executed.
    --gtest_shuffle[-s] BOOL                           Runner will shuffle test and test case
                                                       execution order.
    --gtest_repeat[-i] REPEAT                          Runner will run REPEAT iterations of
                                                       each test case. If -1 will run until killed.
    --gtest_random_seed[-r] SEED                       Runner will use SEED for random decisions.
    --gtest_break_on_failure[-b] BOOL                  Runner will break upon encountering the first
                                                       fatal failure.
    --gtest_also_run_disabled_tests[-a] BOOL           Runner will include test and testcases prefixed with
                                                       'DISABLED_' for execution and listing.
)";

}  // namespace

void Options::Usage(char* bin, LogSink* sink) {
  sink->Write("    Usage: %s  [OPTIONS]\n", bin);
  sink->Write(kUsageMsg);
}

Options Options::FromArgs(int argc, char** argv, fbl::Vector<fbl::String>* errors) {
  // Reset index of parsed arguments.
  optind = 0;
  static const struct option opts[] = {
      {"help", optional_argument, nullptr, 'h'},
      {"gtest_filter", optional_argument, nullptr, 'f'},
      {"gtest_list_tests", optional_argument, nullptr, 'l'},
      {"gtest_shuffle", optional_argument, nullptr, 's'},
      {"gtest_also_run_disabled_tests", optional_argument, nullptr, 'a'},
      {"gtest_repeat", required_argument, nullptr, 'i'},
      {"gtest_random_seed", required_argument, nullptr, 'r'},
      {"gtest_break_on_failure", optional_argument, nullptr, 'b'},
      {0, 0, 0, 0},
  };
  Runner::Options options;

  auto reset = fbl::MakeAutoCall([]() { optind = 0; });

  int c = -1;
  int option_index = -1;
  char* val = nullptr;

  // Pick a random seed by default. Overwrite it if a value was explicitly set.
  options.seed = static_cast<int>(time(nullptr));

  while ((c = getopt_long(argc, argv, "f::l::b::s::a::i:r:h::", opts, &option_index)) >= 0) {
    val = optarg;
    if (val == nullptr) {
      // Verifies that the flag value could be in the form -f value not just -fValue.
      val = GetOptArg(optind, argc, argv);
      if (val != nullptr) {
        ++optind;
      }
    }

    switch (c) {
      case 'h':
        options.help = GetBoolFlag(val);
        return options;
      case 'f':
        // -f with no args resets the filter.
        options.filter = (val == nullptr ? "" : val);
        break;
      case 'l':
        options.list = GetBoolFlag(val);
        break;
      case 's':
        options.shuffle = GetBoolFlag(val);
        break;
      case 'i': {
        int iters = atoi(val);
        if (iters < -1 || iters == 0) {
          options.help = true;
          errors->push_back(fbl::StringPrintf(
              "--gtest_repeat(-i) must take a positive value or -1. (value was %d)", iters));
          return options;
        }
        options.repeat = iters;
        break;
      }
      case 'r':
        options.seed = atoi(val);
        break;
      case 'b':
        options.break_on_failure = GetBoolFlag(val);
        break;
      case 'a':
        options.run_disabled = GetBoolFlag(val);
        break;
    }
  }

  return options;
}
}  // namespace zxtest
