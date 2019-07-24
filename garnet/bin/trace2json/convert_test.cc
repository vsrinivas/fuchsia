// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace2json/convert.h"

#include <gtest/gtest.h>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <src/lib/files/file.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <filesystem>
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

// The gzip header contains a marker byte at offset 9 that contains which OS the file was generated
// on. The gzip files in test_data/ were generated on linux, so a naive comparison leads to the
// tests failing when they run on mac.
const ssize_t kNoIgnores[] = {-1};
const ssize_t kIgnoreGzipOs[] = {9, -1};

void ConvertAndCompare(ConvertSettings settings, std::string expected_output_file,
                       const ssize_t* ignored_offsets) {
  ASSERT_TRUE(ConvertTrace(settings));
  std::string actual_out, expected_out;
  EXPECT_TRUE(files::ReadFileToString(settings.output_file_name, &actual_out));
  EXPECT_TRUE(files::ReadFileToString(expected_output_file, &expected_out));

  for (int i = 0; ignored_offsets[i] >= 0; i++) {
    const size_t offset = ignored_offsets[i];
    if (offset < actual_out.length()) {
      actual_out[offset] = '\0';
    }
    if (offset < expected_out.length()) {
      expected_out[offset] = '\0';
    }
  }

  // Not using EXPECT_EQ here as the trace files can be large, so failures create an unreasonable
  // amount of error output.
  EXPECT_TRUE(actual_out == expected_out)
      << "Files " << settings.output_file_name << " and " << expected_output_file << " differ.";
}

TEST(ConvertTest, SimpleTrace) {
  // simple_trace.fxt is a small hand-written trace file that exercises a few
  // basic event types (currently slice begin, slice end, slice complete, async
  // begin, and async end), and includes both inline and table referenced
  // strings. It only contains one provider.
  std::string test_data_path = GetTestDataPath();
  ConvertSettings settings;
  settings.input_file_name = test_data_path + "simple_trace.fxt";
  settings.output_file_name = test_data_path + "simple_trace_actual.json";
  ConvertAndCompare(settings, test_data_path + "simple_trace_expected.json", kNoIgnores);
}

TEST(ConvertTest, ExampleBenchmark) {
  // example_benchmark.fxt is the trace written by the program in
  // garnet/examples/benchmark, in this case run on qemu. To collect the trace,
  // include //garnet/packages/examples:benchmark in your build and then run
  // `fx traceutil record -binary -spawn /bin/run
  // fuchsia-pkg://fuchsia.com/benchmark#meta/benchmark.cmx`
  std::string test_data_path = GetTestDataPath();
  ConvertSettings settings;
  settings.input_file_name = test_data_path + "example_benchmark.fxt";
  settings.output_file_name = test_data_path + "example_benchmark_actual.json";
  ConvertAndCompare(settings, test_data_path + "example_benchmark_expected.json", kNoIgnores);
}

TEST(ConvertTest, SimpleTraceCompressedOutput) {
  // simple_trace.fxt is a small hand-written trace file that exercises a few
  // basic event types (currently slice begin, slice end, slice complete, async
  // begin, and async end), and includes both inline and table referenced
  // strings. It only contains one provider.
  std::string test_data_path = GetTestDataPath();
  ConvertSettings settings;
  settings.input_file_name = test_data_path + "simple_trace.fxt";
  settings.output_file_name = test_data_path + "simple_trace_actual.json.gz";
  settings.compressed_output = true;
  ConvertAndCompare(settings, test_data_path + "simple_trace_expected.json.gz", kIgnoreGzipOs);
}

TEST(ConvertTest, SimpleTraceCompressedInput) {
  // simple_trace.fxt is a small hand-written trace file that exercises a few
  // basic event types (currently slice begin, slice end, slice complete, async
  // begin, and async end), and includes both inline and table referenced
  // strings. It only contains one provider.
  std::string test_data_path = GetTestDataPath();
  ConvertSettings settings;
  settings.input_file_name = test_data_path + "simple_trace.fxt.gz";
  settings.output_file_name = test_data_path + "simple_trace_gz_actual.json";
  settings.compressed_input = true;
  ConvertAndCompare(settings, test_data_path + "simple_trace_expected.json", kNoIgnores);
}

TEST(ConvertTest, SimpleTraceCompressedInputAndOutput) {
  // simple_trace.fxt is a small hand-written trace file that exercises a few
  // basic event types (currently slice begin, slice end, slice complete, async
  // begin, and async end), and includes both inline and table referenced
  // strings. It only contains one provider.
  std::string test_data_path = GetTestDataPath();
  ConvertSettings settings;
  settings.input_file_name = test_data_path + "simple_trace.fxt.gz";
  settings.output_file_name = test_data_path + "simple_trace_gz_actual.json.gz";
  settings.compressed_input = true;
  settings.compressed_output = true;
  ConvertAndCompare(settings, test_data_path + "simple_trace_expected.json.gz", kIgnoreGzipOs);
}

TEST(ConvertTest, MissingMagicNumber) {
  std::string test_data_path = GetTestDataPath();
  ConvertSettings settings;
  settings.input_file_name = test_data_path + "no_magic.fxt";
  settings.output_file_name = test_data_path + "no_magic.json";
  EXPECT_FALSE(ConvertTrace(settings));
  EXPECT_FALSE(std::filesystem::exists(settings.output_file_name));
}

}  // namespace
