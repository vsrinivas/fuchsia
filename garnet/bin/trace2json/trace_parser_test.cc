// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace2json/trace_parser.h"

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <src/lib/files/file.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <fstream>
#include <sstream>

namespace {

using ::testing::Test;

std::string GetSelfPath() {
  std::string result;
#if defined(__APPLE__)
  // Executable path can have relative references ("..") depending on how the
  // app was launched.
  uint32_t length = 0;
  _NSGetExecutablePath(nullptr, &length);
  result.resize(length);
  _NSGetExecutablePath(&result[0], &length);
  result.resize(length - 1);  // Length included terminator.
#elif defined(__linux__)
  // The realpath() call below will resolve the symbolic link.
  result.assign("/proc/self/exe");
#else
#error Write this for your platform.
#endif

  char fullpath[PATH_MAX];
  return std::string(realpath(result.c_str(), fullpath));
}

std::string GetTestDataPath() {
  std::string path = GetSelfPath();
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos) {
    path = "./";
  } else {
    path.resize(last_slash + 1);
  }
  return path + "test_data/trace2json/";
}

void ConvertAndCompare(std::string input_file,
                       std::string expected_output_file) {
  std::string test_data_path = GetTestDataPath();
  std::ifstream fxt_in(test_data_path + input_file,
                       std::ios_base::in | std::ios_base::binary);

  std::ostringstream json_out;
  // The inner scope here is necessary since ChromiumExporter uses its
  // destructor as the signal to write systemTraceEvents.
  {
    tracing::FuchsiaTraceParser parser(&json_out);
    EXPECT_TRUE(parser.ParseComplete(&fxt_in));
  }

  std::string actual_out = json_out.str();

  std::string expected_out;
  EXPECT_TRUE(files::ReadFileToString(test_data_path + expected_output_file,
                                      &expected_out));

  EXPECT_EQ(actual_out, expected_out);
}

TEST(TraceParserTest, SimpleTrace) {
  // simple_trace.fxt is a small hand-written trace file that exercises a few
  // basic event types (currently slice begin, slice end, slice complete, async
  // begin, and async end), and includes both inline and table referenced
  // strings. It only contains one provider.
  ConvertAndCompare("simple_trace.fxt", "simple_trace.json");
}

TEST(TraceParserTest, ExampleBenchmark) {
  // example_benchmark.fxt is the trace written by the program in
  // garnet/examples/benchmark, in this case run on qemu. To collect the trace,
  // include //garnet/packages/examples:benchmark in your build and then run
  // `fx traceutil record -binary -spawn /bin/run
  // fuchsia-pkg://fuchsia.com/benchmark#meta/benchmark.cmx`
  ConvertAndCompare("example_benchmark.fxt", "example_benchmark.json");
}

TEST(TraceParserTest, InvalidTrace) {
  std::istringstream input("asdfasdfasdfasdfasdf");
  std::ostringstream output;

  {
    tracing::FuchsiaTraceParser parser(&output);
    EXPECT_FALSE(parser.ParseComplete(&input));
  }
}

}  // namespace
