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

bool TestFileComponentInfoTest() {
  BEGIN_TEST;

  ComponentInfo v1_info, v2_info;

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/system/test", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/foo", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/foo/", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/foo/bar", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/foo/bar/", &v1_info, &v2_info);
  EXPECT_STR_EQ("", v1_info.component_url.c_str());
  EXPECT_STR_EQ("", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("", v2_info.component_url.c_str());
  EXPECT_STR_EQ("", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/", &v1_info, &v2_info);
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/bar.cmx", v1_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/meta/bar.cmx", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/bar.cm", v2_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/meta/bar.cm", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/test_file", &v1_info, &v2_info);
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/test_file.cmx",
                v1_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/foo/meta/test_file.cmx", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/test_file.cm", v2_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/foo/meta/test_file.cm", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/test/file", &v1_info, &v2_info);
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/file.cmx", v1_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/foo/bar/meta/file.cmx", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/file.cm", v2_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/foo/bar/meta/file.cm", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/test/file/", &v1_info, &v2_info);
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/file.cmx", v1_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/foo/bar/meta/file.cmx", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/file.cm", v2_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/foo/bar/meta/file.cm", v2_info.manifest_path.c_str());

  v1_info.component_url = "";
  v1_info.manifest_path = "";
  v2_info.component_url = "";
  v2_info.manifest_path = "";
  TestFileComponentInfo("/pkgfs/packages/pname/0/test/disabled/test_name", &v1_info, &v2_info);
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/test_name.cmx",
                v1_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/0/meta/test_name.cmx", v1_info.manifest_path.c_str());
  EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/test_name.cm", v2_info.component_url.c_str());
  EXPECT_STR_EQ("/pkgfs/packages/pname/0/meta/test_name.cm", v2_info.manifest_path.c_str());

  END_TEST;
}

static ScopedTestFile NewPublishFile(const fbl::String& test_name) {
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (root_dir == nullptr) {
    root_dir = "";
  }
  const fbl::String path = fbl::String::Concat({root_dir, "/bin/publish-data-helper"});
  return ScopedTestFile(test_name.c_str(), path.c_str());
}

bool RunTestDontPublishData() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String test_name = JoinPath(test_dir.path(), "publish-data-helper");
  auto file = NewPublishFile(test_name);

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
  fbl::String test_name = JoinPath(test_dir.path(), "publish-data-helper");
  auto file = NewPublishFile(test_name);
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
  fbl::String test_name = JoinPath(test_dir.path(), "publish-data-helper");
  auto file = NewPublishFile(test_name);
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
  fbl::String test_name = JoinPath(test_dir.path(), "publish-data-helper");
  auto file = NewPublishFile(test_name);

  const fbl::String output_dir = JoinPath(test_dir.path(), "run-all-tests-output-1");
  EXPECT_EQ(0, MkDirAll(output_dir));

  const char* const argv[] = {"./runtests", "-o", output_dir.c_str(), test_dir.path()};
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
      GetOutputFileRelPath(output_dir, JoinPath(test_name, "test"), &test_data_sink_rel_path));

  fbl::StringBuffer<1024> expected_data_sink_buf;
  expected_data_sink_buf.AppendPrintf(
      "        \"test\": [\n"
      "          {\n"
      "            \"name\": \"test\",\n"
      "            \"file\": \"%s\"\n"
      "          }\n"
      "        ]",
      test_data_sink_rel_path.c_str() + 1);  // +1 to discard the leading slash.

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

bool RunTestRootDir() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String test_name = JoinPath(test_dir.path(), "succeed.sh");
  const char* argv[] = {test_name.c_str(), nullptr};

  // This test should have gotten TEST_ROOT_DIR. Confirm that we can find our
  // artifact in the "testdata/" directory under TEST_ROOT_DIR.
  const char* root_dir = getenv("TEST_ROOT_DIR");
  if (!root_dir) {
    root_dir = "";
  }

  // Run a test and confirm TEST_ROOT_DIR gets passed along.
  {
    const char script_contents[] =
        "read line < $TEST_ROOT_DIR/testdata/runtests-utils/test-data\n"
        "echo \"$line\"\n";
    ScopedScriptFile script(argv[0], script_contents);
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

BEGIN_TEST_CASE(FuchsiaComponentInfo)
RUN_TEST_SMALL(TestFileComponentInfoTest)
END_TEST_CASE(FuchsiaComponentInfo)

BEGIN_TEST_CASE(RunTests)
RUN_TEST(RunTestDontPublishData)
RUN_TEST_MEDIUM(RunTestsPublishData)
RUN_TEST_MEDIUM(RunDuplicateTestsPublishData)
RUN_TEST_MEDIUM(RunAllTestsPublishData)
RUN_TEST_MEDIUM(RunTestRootDir)
END_TEST_CASE(RunTests)

}  // namespace
}  // namespace runtests
