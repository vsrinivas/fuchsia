// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <regex>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <runtests-utils/fuchsia-run-test.h>
#include <runtests-utils/runtests-utils.h>
#include <unittest/unittest.h>

#include "runtests-utils-test-utils.h"

namespace runtests {
namespace {

bool SetUpForTestComponentCMX() {
  BEGIN_TEST;
  fbl::String component_executor;
  EXPECT_TRUE(SetUpForTestComponent("fuchsia-pkg://fuchsia.com/foo-tests#meta/bar.cmx",
                                    &component_executor));
  EXPECT_GT(component_executor.length(), 0);
  END_TEST;
}

bool SetUpForTestComponentCM() {
  BEGIN_TEST;
  fbl::String component_executor;
  EXPECT_TRUE(SetUpForTestComponent("fuchsia-pkg://fuchsia.com/foo-tests#meta/bar.cm",
                                    &component_executor));
  EXPECT_GT(component_executor.length(), 0);
  END_TEST;
}

bool SetUpForTestComponentBadURI() {
  BEGIN_TEST;
  fbl::String component_executor;
  EXPECT_FALSE(SetUpForTestComponent("fuchsia-pkg://fuchsia.com/foo-tests#meta/bar.xyz",
                                     &component_executor));
  EXPECT_EQ(component_executor.length(), 0);
  END_TEST;
}

bool SetUpForTestComponentPkgFS() {
  BEGIN_TEST;
  fbl::String component_executor;
  EXPECT_FALSE(SetUpForTestComponent("/pkgfs/packages/foo-tests/bar", &component_executor));
  EXPECT_EQ(component_executor.length(), 0);
  END_TEST;
}

bool SetUpForTestComponentPath() {
  BEGIN_TEST;
  fbl::String component_executor;
  EXPECT_TRUE(SetUpForTestComponent("/boot/test/foo", &component_executor));
  EXPECT_EQ(component_executor.length(), 0);
  END_TEST;
}

static fbl::String PublishDataHelperDir() {
  return JoinPath(packaged_script_dir(), "publish-data");
}

static fbl::String PublishDataHelperBin() {
  return JoinPath(PublishDataHelperDir(), "publish-data-helper");
}

static fbl::String ProfileHelperDir() {
  return JoinPath(packaged_script_dir(), "profile");
}

static fbl::String ProfileHelperBin() {
  return JoinPath(ProfileHelperDir(), "profile-helper");
}

bool RunTestDontPublishData() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String test_name = PublishDataHelperBin();

  const char* argv[] = {test_name.c_str(), nullptr};
  std::unique_ptr<Result> result = RunTest(argv, nullptr, nullptr, test_name.c_str(), 0);
  EXPECT_STR_EQ(argv[0], result->name.c_str());
  EXPECT_EQ(SUCCESS, result->launch_status);
  EXPECT_EQ(0, result->return_code);
  EXPECT_EQ(0, result->data_sinks.size());

  END_TEST;
}

bool RunTestsPublishData() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String test_name = PublishDataHelperBin();
  int num_failed = 0;
  fbl::Vector<std::unique_ptr<Result>> results;
  const signed char verbosity = 77;
  const fbl::String output_dir = JoinPath(test_dir.path(), "output");
  const char output_file_base_name[] = "output.txt";
  ASSERT_EQ(0, MkDirAll(output_dir));
  EXPECT_TRUE(RunTests({test_name}, {}, 1, 0, output_dir.c_str(), output_file_base_name, verbosity,
                       &num_failed, &results));
  EXPECT_EQ(0, num_failed);
  EXPECT_EQ(1, results.size());
  EXPECT_LE(1, results[0]->data_sinks.size());

  END_TEST;
}

bool RunDuplicateTestsPublishData() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String test_name = PublishDataHelperBin();
  int num_failed = 0;
  fbl::Vector<std::unique_ptr<Result>> results;
  const signed char verbosity = 77;
  const fbl::String output_dir = JoinPath(test_dir.path(), "output");
  const char output_file_base_name[] = "output.txt";
  ASSERT_EQ(0, MkDirAll(output_dir));
  EXPECT_TRUE(RunTests({test_name, test_name, test_name}, {}, 1, 0, output_dir.c_str(),
                       output_file_base_name, verbosity, &num_failed, &results));
  EXPECT_EQ(0, num_failed);
  EXPECT_EQ(3, results.size());
  EXPECT_STR_EQ(test_name.c_str(), results[0]->name.c_str());
  EXPECT_STR_EQ(fbl::String::Concat({test_name, " (2)"}).c_str(), results[1]->name.c_str());
  EXPECT_STR_EQ(fbl::String::Concat({test_name, " (3)"}).c_str(), results[2]->name.c_str());

  END_TEST;
}

bool RunAllTestsPublishData() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String test_containing_dir = PublishDataHelperDir();
  fbl::String test_name = PublishDataHelperBin();

  const fbl::String output_dir = JoinPath(test_dir.path(), "run-all-tests-output-1");
  EXPECT_EQ(0, MkDirAll(output_dir));

  const char* const argv[] = {"./runtests", "-o", output_dir.c_str(), test_containing_dir.c_str()};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_SUCCESS, DiscoverAndRunTests(4, argv, {}, &stopwatch, ""));

  // Prepare the expected output.
  fbl::String test_output_rel_path;
  ASSERT_TRUE(GetOutputFileRelPath(output_dir, test_name, &test_output_rel_path));

  fbl::StringBuffer<1024> expected_output_buf;
  expected_output_buf.AppendPrintf(
      R"(
      "name": "%s",
      "output_file": "%s",
      "result": "PASS",
      "duration_milliseconds": \d+)",
      test_name.c_str(),
      test_output_rel_path.c_str() + 1);  // +1 to discard the leading slash.
  std::regex expected_output_regex(expected_output_buf.c_str());

  fbl::String test_data_sink_rel_path;
  ASSERT_TRUE(
      GetOutputFileRelPath(output_dir, "test", &test_data_sink_rel_path));

  fbl::StringBuffer<1024> expected_data_sink_buf;
  expected_data_sink_buf.AppendPrintf(
      "        \"test\": [\n"
      "          {\n"
      "            \"name\": \"test\",\n"
      "            \"file\": \"%s\"\n"
      "          }\n"
      "        ]",
      test_data_sink_rel_path.c_str());

  // Extract the actual output.
  const fbl::String output_path = JoinPath(output_dir, "summary.json");
  FILE* output_file = fopen(output_path.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);

  EXPECT_TRUE(std::regex_search(buf, expected_output_regex));
  EXPECT_NONNULL(strstr(buf, expected_data_sink_buf.c_str()));

  END_TEST;
}

bool RunProfileMergeData() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String test_name = ProfileHelperBin();
  int num_failed = 0;
  fbl::Vector<std::unique_ptr<Result>> results;
  const signed char verbosity = 77;
  const fbl::String output_dir = JoinPath(test_dir.path(), "output");
  const char output_file_base_name[] = "output.txt";
  ASSERT_EQ(0, MkDirAll(output_dir));

  // Run the test for the first time.
  EXPECT_TRUE(RunTests({test_name}, {}, 1, 0, output_dir.c_str(), output_file_base_name, verbosity,
                       &num_failed, &results));
  EXPECT_EQ(0, num_failed);
  EXPECT_EQ(1, results.size());
  EXPECT_LE(1, results[0]->data_sinks.size());
  EXPECT_TRUE(results[0]->data_sinks.find("llvm-profile") != results[0]->data_sinks.end());
  EXPECT_EQ(1, results[0]->data_sinks["llvm-profile"].size());

  // Run the test for the second time.
  EXPECT_TRUE(RunTests({test_name}, {}, 1, 0, output_dir.c_str(), output_file_base_name, verbosity,
                       &num_failed, &results));
  EXPECT_EQ(0, num_failed);
  EXPECT_EQ(2, results.size());
  EXPECT_LE(1, results[1]->data_sinks.size());
  EXPECT_TRUE(results[1]->data_sinks.find("llvm-profile") != results[1]->data_sinks.end());
  EXPECT_EQ(1, results[1]->data_sinks["llvm-profile"].size());

  // Check that the data was merged (i.e. they're the same).
  EXPECT_TRUE(results[0]->data_sinks["llvm-profile"][0].file == results[1]->data_sinks["llvm-profile"][0].file);

  END_TEST;
}

bool RunTestRootDir() {
  BEGIN_TEST;

  PackagedScriptFile test_script("test-root-dir.sh");
  fbl::String test_name = test_script.path();
  const char* argv[] = {test_name.c_str(), nullptr};
  ScopedTestDir test_dir;

  // This test should have gotten TEST_ROOT_DIR. Confirm that we can find our
  // artifact in the "testdata/" directory under TEST_ROOT_DIR.
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (!root_dir) {
    root_dir = "";
  }

  // Run a test and confirm TEST_ROOT_DIR gets passed along.
  {
    fbl::String output_filename = JoinPath(test_dir.path(), "test.out");
    std::unique_ptr<Result> result =
        RunTest(argv, nullptr, output_filename.c_str(), test_name.c_str(), 0);

    FILE* output_file = fopen(output_filename.c_str(), "r");
    ASSERT_TRUE(output_file);
    char* line = nullptr;
    size_t n = 0;
    ASSERT_LE(0, getline(&line, &n, output_file));
    EXPECT_STR_EQ("Hello world!\n", line);
    fclose(output_file);
    free(line);
    EXPECT_STR_EQ(argv[0], result->name.c_str());
    EXPECT_EQ(SUCCESS, result->launch_status);
    EXPECT_EQ(0, result->return_code);
  }

  END_TEST;
}

BEGIN_TEST_CASE(SetUpForTestComponent)
RUN_TEST(SetUpForTestComponentCMX)
RUN_TEST(SetUpForTestComponentCM)
RUN_TEST(SetUpForTestComponentBadURI)
RUN_TEST(SetUpForTestComponentPkgFS)
RUN_TEST(SetUpForTestComponentPath)
END_TEST_CASE(SetUpForTestComponent)

BEGIN_TEST_CASE(RunTests)
RUN_TEST(RunTestDontPublishData)
RUN_TEST_MEDIUM(RunTestsPublishData)
RUN_TEST_MEDIUM(RunDuplicateTestsPublishData)
RUN_TEST_MEDIUM(RunAllTestsPublishData)
RUN_TEST_MEDIUM(RunProfileMergeData)
RUN_TEST_MEDIUM(RunTestRootDir)
END_TEST_CASE(RunTests)

}  // namespace
}  // namespace runtests
