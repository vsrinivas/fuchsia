// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <regex>

#include <fbl/auto_call.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <runtests-utils/runtests-utils.h>
#include <unittest/unittest.h>

#include "runtests-utils-test-utils.h"

namespace runtests {
namespace {

static constexpr size_t kOneMegabyte = 1 << 20;

bool ParseTestNamesEmptyStr() {
  BEGIN_TEST;

  fbl::String input("");
  fbl::Vector<fbl::String> parsed;
  ParseTestNames(input, &parsed);
  EXPECT_EQ(0, parsed.size());

  END_TEST;
}

bool ParseTestNamesEmptyStrInMiddle() {
  BEGIN_TEST;

  fbl::String input("a,,b");
  fbl::Vector<fbl::String> parsed;
  ParseTestNames(input, &parsed);
  ASSERT_EQ(2, parsed.size());
  EXPECT_STR_EQ("a", parsed[0].c_str());
  EXPECT_STR_EQ("b", parsed[1].c_str());

  END_TEST;
}

bool ParseTestNamesTrailingComma() {
  BEGIN_TEST;

  fbl::String input("a,");
  fbl::Vector<fbl::String> parsed;
  ParseTestNames(input, &parsed);
  ASSERT_EQ(1, parsed.size());
  EXPECT_STR_EQ("a", parsed[0].c_str());

  END_TEST;
}

bool ParseTestNamesNormal() {
  BEGIN_TEST;

  fbl::String input("a,b");
  fbl::Vector<fbl::String> parsed;
  ParseTestNames(input, &parsed);
  ASSERT_EQ(2, parsed.size());
  EXPECT_STR_EQ("a", parsed[0].c_str());
  EXPECT_STR_EQ("b", parsed[1].c_str());

  END_TEST;
}

bool EmptyWhitelist() {
  BEGIN_TEST;

  fbl::Vector<fbl::String> whitelist;
  EXPECT_FALSE(IsInWhitelist("a", whitelist));

  END_TEST;
}

bool NonemptyWhitelist() {
  BEGIN_TEST;

  fbl::Vector<fbl::String> whitelist = {"b", "a"};
  EXPECT_TRUE(IsInWhitelist("a", whitelist));

  END_TEST;
}

bool JoinPathNoTrailingSlash() {
  BEGIN_TEST;

  EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b", "c/d").c_str());

  END_TEST;
}

bool JoinPathTrailingSlash() {
  BEGIN_TEST;

  EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b/", "c/d").c_str());

  END_TEST;
}

bool JoinPathAbsoluteChild() {
  BEGIN_TEST;

  EXPECT_STR_EQ("a/b/c/d", JoinPath("a/b/", "/c/d").c_str());

  END_TEST;
}

bool MkDirAllTooLong() {
  BEGIN_TEST;

  char too_long[PATH_MAX + 2];
  memset(too_long, 'a', PATH_MAX + 1);
  too_long[PATH_MAX + 1] = '\0';
  EXPECT_EQ(ENAMETOOLONG, MkDirAll(too_long));

  END_TEST;
}
bool MkDirAllAlreadyExists() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  const fbl::String already = JoinPath(test_dir.path(), "already");
  const fbl::String exists = JoinPath(already, "exists");
  ASSERT_EQ(0, mkdir(already.c_str(), 0755));
  ASSERT_EQ(0, mkdir(exists.c_str(), 0755));
  EXPECT_EQ(0, MkDirAll(exists));

  END_TEST;
}
bool MkDirAllParentAlreadyExists() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  const fbl::String parent = JoinPath(test_dir.path(), "existing-parent");
  const fbl::String child = JoinPath(parent, "child");
  ASSERT_EQ(0, mkdir(parent.c_str(), 0755));
  EXPECT_EQ(0, MkDirAll(child));
  struct stat s;
  EXPECT_EQ(0, stat(child.c_str(), &s));

  END_TEST;
}
bool MkDirAllParentDoesNotExist() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  const fbl::String parent = JoinPath(test_dir.path(), "not-existing-parent");
  const fbl::String child = JoinPath(parent, "child");
  struct stat s;
  ASSERT_NE(0, stat(parent.c_str(), &s));
  EXPECT_EQ(0, MkDirAll(child));
  EXPECT_EQ(0, stat(child.c_str(), &s));

  END_TEST;
}

bool WriteSummaryJSONSucceeds() {
  BEGIN_TEST;

  // A reasonable guess that the function won't output more than this.
  std::unique_ptr<char[]> buf(new char[kOneMegabyte]);
  FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
  fbl::Vector<std::unique_ptr<Result>> results;
  results.push_back(std::make_unique<Result>("/a", SUCCESS, 0, 10));
  results.push_back(std::make_unique<Result>("b", FAILED_TO_LAUNCH, 0, 0));
  ASSERT_EQ(0, WriteSummaryJSON(results, "output.txt", "/tmp/file_path", buf_file));
  fclose(buf_file);
  // We don't have a JSON parser in zircon right now, so just hard-code the
  // expected output.
  const char kExpectedJSONOutput[] = R"({
  "tests": [
    {
      "name": "/a",
      "output_file": "a/output.txt",
      "result": "PASS",
      "duration_milliseconds": 10
    },
    {
      "name": "b",
      "output_file": "b/output.txt",
      "result": "FAIL",
      "duration_milliseconds": 0
    }
  ],
  "outputs": {
    "syslog_file": "/tmp/file_path"
  }
}
)";
  EXPECT_STR_EQ(kExpectedJSONOutput, buf.get());

  END_TEST;
}

bool WriteSummaryJSONSucceedsWithoutSyslogPath() {
  BEGIN_TEST;

  // A reasonable guess that the function won't output more than this.
  std::unique_ptr<char[]> buf(new char[kOneMegabyte]);
  FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
  fbl::Vector<std::unique_ptr<Result>> results;
  results.push_back(std::make_unique<Result>("/a", SUCCESS, 0, 10));
  results.push_back(std::make_unique<Result>("b", FAILED_TO_LAUNCH, 0, 0));
  ASSERT_EQ(0, WriteSummaryJSON(results, "output.txt", /*syslog_path=*/"", buf_file));
  fclose(buf_file);
  // With an empty syslog_path, we expect no values under "outputs" and
  // "syslog_file" to be generated in the JSON output.
  const char kExpectedJSONOutput[] = R"({
  "tests": [
    {
      "name": "/a",
      "output_file": "a/output.txt",
      "result": "PASS",
      "duration_milliseconds": 10
    },
    {
      "name": "b",
      "output_file": "b/output.txt",
      "result": "FAIL",
      "duration_milliseconds": 0
    }
  ]
}
)";

  EXPECT_STR_EQ(kExpectedJSONOutput, buf.get());
  END_TEST;
}

bool WriteSummaryJSONBadTestName() {
  BEGIN_TEST;

  // A reasonable guess that the function won't output more than this.
  std::unique_ptr<char[]> buf(new char[kOneMegabyte]);
  FILE* buf_file = fmemopen(buf.get(), kOneMegabyte, "w");
  // A test name and output file consisting entirely of slashes should trigger
  // an error.
  fbl::Vector<std::unique_ptr<Result>> results;
  results.push_back(std::make_unique<Result>("///", SUCCESS, 0, 10));
  results.push_back(std::make_unique<Result>("b", FAILED_TO_LAUNCH, 0, 10));
  ASSERT_NE(0, WriteSummaryJSON(results, /*output_file_basename=*/"///",
                                /*syslog_path=*/"/", buf_file));
  fclose(buf_file);

  END_TEST;
}

bool ResolveGlobsNoMatches() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::Vector<fbl::String> resolved;
  fbl::String test_fs_glob = JoinPath(test_dir.path(), "bar*");
  const fbl::Vector<fbl::String> globs = {"/foo/bar/*", test_fs_glob};
  ASSERT_EQ(0, ResolveGlobs(globs, &resolved));
  EXPECT_EQ(0, resolved.size());

  END_TEST;
}

bool ResolveGlobsMultipleMatches() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  fbl::String existing_dir_path = JoinPath(test_dir.path(), "existing-dir/prefix-suffix");
  fbl::String existing_file_path = JoinPath(test_dir.path(), "existing-file");
  fbl::String existing_dir_glob = JoinPath(test_dir.path(), "existing-dir/prefix*");
  const fbl::Vector<fbl::String> globs = {"/does/not/exist/*",
                                          existing_dir_glob,  // matches existing_dir_path.
                                          existing_file_path};
  ASSERT_EQ(0, MkDirAll(existing_dir_path));
  const int existing_file_fd = open(existing_file_path.c_str(), O_CREAT);
  ASSERT_NE(-1, existing_file_fd, strerror(errno));
  ASSERT_NE(-1, close(existing_file_fd), strerror(errno));
  fbl::Vector<fbl::String> resolved;
  ASSERT_EQ(0, ResolveGlobs(globs, &resolved));
  ASSERT_EQ(2, resolved.size());
  EXPECT_STR_EQ(existing_dir_path.c_str(), resolved[0].c_str());

  END_TEST;
}

bool RunTestSuccess() {
  BEGIN_TEST;

  PackagedScriptFile script_file("succeed.sh");
  fbl::String test_name = script_file.path();
  const char* argv[] = {test_name.c_str(), nullptr};
  std::unique_ptr<Result> result = RunTest(argv, nullptr, nullptr, test_name.c_str(), 0);
  EXPECT_STR_EQ(argv[0], result->name.c_str());
  EXPECT_EQ(SUCCESS, result->launch_status);
  EXPECT_EQ(0, result->return_code);

  END_TEST;
}

bool RunTestTimeout() {
  BEGIN_TEST;

  // Test timeout is enforced if the test runs too long.
  PackagedScriptFile inf_loop_file("test-inf-loop.sh");
  fbl::String inf_loop_name = inf_loop_file.path();
  const char* inf_loop_argv[] = {inf_loop_name.c_str(), nullptr};
  std::unique_ptr<Result> result =
      RunTest(inf_loop_argv, nullptr, nullptr, inf_loop_name.c_str(), 1);
  EXPECT_STR_EQ(inf_loop_argv[0], result->name.c_str());
  EXPECT_EQ(TIMED_OUT, result->launch_status);
  EXPECT_EQ(0, result->return_code);

  // Test timeout is not enforced if the test finishes quickly.
  PackagedScriptFile success_file("succeed.sh");
  fbl::String succeed_name = success_file.path();
  const char* succeed_argv[] = {succeed_name.c_str(), nullptr};
  result = RunTest(succeed_argv, nullptr, nullptr, succeed_name.c_str(), 100000);
  EXPECT_STR_EQ(succeed_argv[0], result->name.c_str());
  EXPECT_EQ(SUCCESS, result->launch_status);
  EXPECT_EQ(0, result->return_code);

  // Still works if output file set.
  ScopedTestDir test_dir;
  fbl::String output_filename = JoinPath(test_dir.path(), "test-inf-loop.out");
  result = RunTest(inf_loop_argv, nullptr, output_filename.c_str(), inf_loop_name.c_str(), 1);
  EXPECT_STR_EQ(inf_loop_argv[0], result->name.c_str());
  EXPECT_EQ(TIMED_OUT, result->launch_status);
  EXPECT_EQ(0, result->return_code);

  END_TEST;
}

bool RunTestSuccessWithStdout() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  PackagedScriptFile script_file("expect-this-success.sh");
  fbl::String test_name = script_file.path();
  const char* argv[] = {test_name.c_str(), nullptr};
  const char expected_output[] = "Expect this!\n";

  fbl::String output_filename = JoinPath(test_dir.path(), "test.out");
  std::unique_ptr<Result> result =
      RunTest(argv, nullptr, output_filename.c_str(), test_name.c_str(), 0);

  FILE* output_file = fopen(output_filename.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);
  EXPECT_STR_EQ(expected_output, buf);
  EXPECT_STR_EQ(argv[0], result->name.c_str());
  EXPECT_EQ(SUCCESS, result->launch_status);
  EXPECT_EQ(0, result->return_code);

  END_TEST;
}

bool RunTestFailureWithStderr() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  PackagedScriptFile script_file("expect-this-failure.sh");
  fbl::String test_name = script_file.path();
  const char* argv[] = {test_name.c_str(), nullptr};
  const char expected_output[] = "Expect this!\n";

  fbl::String output_filename = JoinPath(test_dir.path(), "test.out");
  std::unique_ptr<Result> result =
      RunTest(argv, nullptr, output_filename.c_str(), test_name.c_str(), 0);

  FILE* output_file = fopen(output_filename.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);
  EXPECT_STR_EQ(expected_output, buf);
  EXPECT_STR_EQ(argv[0], result->name.c_str());
  EXPECT_EQ(FAILED_NONZERO_RETURN_CODE, result->launch_status);
  EXPECT_EQ(77, result->return_code);

  END_TEST;
}

bool RunTestFailureToLoadFile() {
  BEGIN_TEST;

  const char* argv[] = {"i/do/not/exist/", nullptr};

  std::unique_ptr<Result> result = RunTest(argv, nullptr, nullptr, argv[0], 0);
  EXPECT_STR_EQ(argv[0], result->name.c_str());
  EXPECT_EQ(FAILED_TO_LAUNCH, result->launch_status);

  END_TEST;
}

bool DiscoverTestsInDirGlobsBasic() {
  BEGIN_TEST;

  ScopedTestDir test_dir;

  const fbl::String a_file_name = JoinPath(test_dir.path(), "a.sh");
  const fbl::String b_file_name = JoinPath(test_dir.path(), "b.sh");
  ScopedStubFile a_file(a_file_name);
  ScopedStubFile b_file(b_file_name);

  fbl::Vector<fbl::String> discovered_paths;
  EXPECT_EQ(0, DiscoverTestsInDirGlobs({test_dir.path()}, nullptr, {}, &discovered_paths));
  EXPECT_EQ(2, discovered_paths.size());
  bool discovered_a = false;
  bool discovered_b = false;
  // The order of the results is not defined, so just check that each is
  // present.
  for (const auto& path : discovered_paths) {
    if (fbl::StringPiece(path) == a_file_name.ToStringPiece()) {
      discovered_a = true;
    } else if (fbl::StringPiece(path) == b_file_name.ToStringPiece()) {
      discovered_b = true;
    }
  }
  EXPECT_TRUE(discovered_a);
  EXPECT_TRUE(discovered_b);

  END_TEST;
}

bool DiscoverTestsInDirGlobsFilter() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  const char kHopefullyUniqueFileBasename[] = "e829cea9919fe045ca199945db7ac99a";
  const fbl::String unique_file_name = JoinPath(test_dir.path(), kHopefullyUniqueFileBasename);
  ScopedStubFile unique_file(unique_file_name);

  // This one should be ignored because its basename is not in the include list.
  const fbl::String other_file_name = JoinPath(test_dir.path(), "foo.sh");
  ScopedStubFile fail_file(other_file_name);

  fbl::Vector<fbl::String> discovered_paths;
  EXPECT_EQ(0, DiscoverTestsInDirGlobs({JoinPath(kMemFsRoot, "*")}, nullptr,
                                       {kHopefullyUniqueFileBasename}, &discovered_paths));
  EXPECT_EQ(1, discovered_paths.size());
  EXPECT_STR_EQ(unique_file_name.c_str(), discovered_paths[0].c_str());

  END_TEST;
}

bool DiscoverTestsInDirGlobsIgnore() {
  BEGIN_TEST;
  ScopedTestDir test_dir_a, test_dir_b;
  const fbl::String a_name = JoinPath(test_dir_a.path(), "foo.sh");
  ScopedStubFile a_file(a_name);
  const fbl::String b_name = JoinPath(test_dir_b.path(), "foo.sh");
  ScopedStubFile fail_file(b_name);
  fbl::Vector<fbl::String> discovered_paths;
  EXPECT_EQ(0, DiscoverTestsInDirGlobs({test_dir_a.path(), test_dir_b.path()},
                                       test_dir_b.basename(), {}, &discovered_paths));
  EXPECT_EQ(1, discovered_paths.size());
  EXPECT_STR_EQ(a_name.c_str(), discovered_paths[0].c_str());
  END_TEST;
}

bool DiscoverTestsInListFileWithTrailingWhitespace() {
  BEGIN_TEST;
  const char* lines[] = {"trailing/tab\t\n", "trailing/space \n", "trailing/return\r"};
  size_t total_size = strlen(lines[0]) + strlen(lines[1]) + strlen(lines[2]);
  std::unique_ptr<char[]> buf(new char[total_size]);
  FILE* test_list_file = fmemopen(buf.get(), total_size, "w");
  fprintf(test_list_file, "%s", lines[0]);
  fprintf(test_list_file, "%s", lines[1]);
  fprintf(test_list_file, "%s", lines[2]);
  fclose(test_list_file);
  test_list_file = fmemopen(buf.get(), total_size, "r");
  fbl::Vector<fbl::String> test_paths;
  EXPECT_EQ(0, DiscoverTestsInListFile(test_list_file, &test_paths));
  EXPECT_EQ(3, test_paths.size());
  EXPECT_STR_EQ("trailing/tab", test_paths[0].c_str());
  EXPECT_STR_EQ("trailing/space", test_paths[1].c_str());
  EXPECT_STR_EQ("trailing/return", test_paths[2].c_str());
  fclose(test_list_file);
  END_TEST;
}

bool RunTestsWithVerbosity() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  PackagedScriptFile succeed_script("succeed-with-echo.sh");
  const fbl::String succeed_file_name = succeed_script.path();
  int num_failed = 0;
  fbl::Vector<std::unique_ptr<Result>> results;
  const signed char verbosity = 77;
  const fbl::String output_dir = JoinPath(test_dir.path(), "output");
  const char output_file_base_name[] = "output.txt";
  ASSERT_EQ(0, MkDirAll(output_dir));
  EXPECT_TRUE(RunTests({succeed_file_name}, {}, 1, 0, output_dir.c_str(), output_file_base_name,
                       verbosity, &num_failed, &results));
  EXPECT_EQ(0, num_failed);
  EXPECT_EQ(1, results.size());

  fbl::String output_path =
      JoinPath(JoinPath(output_dir, succeed_file_name), output_file_base_name);
  FILE* output_file = fopen(output_path.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);
  EXPECT_STR_EQ("Success! v=77\n", buf);

  END_TEST;
}

bool RunTestsWithArguments() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  PackagedScriptFile succeed_script("succeed-with-echo.sh");
  const fbl::String succeed_file_name = succeed_script.path();
  int num_failed = 0;
  const signed char verbosity = -1;
  fbl::Vector<std::unique_ptr<Result>> results;
  fbl::Vector<fbl::String> args{"first", "second", "third", "-4", "--", "-", "seventh"};
  const fbl::String output_dir = JoinPath(test_dir.path(), "output");
  const char output_file_base_name[] = "output.txt";
  ASSERT_EQ(0, MkDirAll(output_dir));
  EXPECT_TRUE(RunTests({succeed_file_name}, args, 1, 0, output_dir.c_str(), output_file_base_name,
                       verbosity, &num_failed, &results));
  EXPECT_EQ(0, num_failed);
  EXPECT_EQ(1, results.size());

  fbl::String output_path =
      JoinPath(JoinPath(output_dir, succeed_file_name), output_file_base_name);
  FILE* output_file = fopen(output_path.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);
  EXPECT_STR_EQ("Success! first second third -4 -- - seventh\n", buf);

  END_TEST;
}

bool RunTestsCreatesOutputFile() {
  BEGIN_TEST;

  // Assert the output file is created, even if the test doesn't execute.
  ScopedTestDir test_dir;
  const fbl::String does_not_exist_file_name = JoinPath(test_dir.path(), "i-do-not-exist.sh");
  int num_failed = 0;
  const signed char verbosity = -1;
  fbl::Vector<std::unique_ptr<Result>> results;
  const fbl::String output_dir = JoinPath(test_dir.path(), "output");
  const char output_file_base_name[] = "output.txt";
  ASSERT_EQ(0, MkDirAll(output_dir));
  EXPECT_TRUE(RunTests({does_not_exist_file_name}, {}, 1, 0, output_dir.c_str(),
                       output_file_base_name, verbosity, &num_failed, &results));
  EXPECT_EQ(1, num_failed);
  EXPECT_EQ(1, results.size());

  fbl::String output_path =
      JoinPath(JoinPath(output_dir, does_not_exist_file_name), output_file_base_name);
  FILE* output_file = fopen(output_path.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_EQ(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);

  END_TEST;
}

bool DiscoverAndRunTestsBasicPass() {
  BEGIN_TEST;

  // The build templates assemble two tests, a.sh and b.sh, in the
  // runtestsbasicpass/ subdirectory.
  const fbl::String script_dir = packaged_script_dir();
  const fbl::String test_script_dir = JoinPath(script_dir, "runtestsbasicpass");
  const char* const argv[] = {"./runtests", test_script_dir.c_str()};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_SUCCESS, DiscoverAndRunTests(2, argv, {}, &stopwatch, ""));

  END_TEST;
}

bool DiscoverAndRunTestsBasicFail() {
  BEGIN_TEST;

  // The build templates assemble two tests, test-basic-succeed.sh and
  // test-basic-fail.sh, in the runtestsbasicfail/ subdirectory.
  const fbl::String script_dir = packaged_script_dir();
  const fbl::String test_script_dir = JoinPath(script_dir, "runtestsbasicfail");
  const char* const argv[] = {"./runtests", test_script_dir.c_str()};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_FAILURE, DiscoverAndRunTests(2, argv, {}, &stopwatch, ""));

  END_TEST;
}

bool DiscoverAndRunTestsFallsBackToDefaultDirs() {
  BEGIN_TEST;

  PackagedScriptFile test_script("succeed-with-echo.sh");
  const char* const argv[] = {"./runtests"};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_SUCCESS, DiscoverAndRunTests(1, argv, {test_script.path()}, &stopwatch, ""));

  END_TEST;
}

bool DiscoverAndRunTestsFailsWithNoTestGlobsOrDefaultDirs() {
  BEGIN_TEST;

  ScopedTestDir test_dir;
  const fbl::String succeed_file_name = JoinPath(test_dir.path(), "succeed.sh");
  ScopedStubFile succeed_file(succeed_file_name);
  const char* const argv[] = {"./runtests"};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_FAILURE, DiscoverAndRunTests(1, argv, {}, &stopwatch, ""));

  END_TEST;
}

bool DiscoverAndRunTestsFailsWithBadArgs() {
  BEGIN_TEST;

  fbl::String script_dir = packaged_script_dir();
  const char* const argv[] = {"./runtests", "-?", "unknown-arg", script_dir.c_str()};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_FAILURE, DiscoverAndRunTests(4, argv, {}, &stopwatch, ""));

  END_TEST;
}

bool DiscoverAndRunTestsWithGlobs() {
  BEGIN_TEST;

  // There are three scripts generated in a directory by the build templates:
  //
  // testglobs/test-globs-root.sh
  // testglobs/A/B/C/test-globs-one.sh
  // testglobs/A/D/C/test-globs-two.sh
  //
  // Verify that we find and run all three.
  fbl::String all_scripts_dir = packaged_script_dir();
  fbl::String script_dir = JoinPath(all_scripts_dir, "testglobs");
  fbl::String glob = JoinPath(script_dir, "A/*/C");
  const char* const argv[] = {"./runtests", script_dir.c_str(), glob.c_str()};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_SUCCESS, DiscoverAndRunTests(3, argv, {}, &stopwatch, ""));

  END_TEST;
}

// Passing an -o argument should result in output being written to that
// location.
bool DiscoverAndRunTestsWithOutput() {
  BEGIN_TEST;

  fbl::String all_scripts_dir = packaged_script_dir();
  fbl::String script_dir = JoinPath(all_scripts_dir, "testwithoutput");

  ScopedTestDir test_dir;

  const fbl::String succeed_file_name = JoinPath(script_dir, "test-with-output-succeed.sh");
  const fbl::String fail_file_name = JoinPath(script_dir, "test-with-output-fail.sh");
  const fbl::String output_dir = JoinPath(test_dir.path(), "run-all-tests-output-1");
  EXPECT_EQ(0, MkDirAll(output_dir));

  const char* const argv[] = {"./runtests", "-o", output_dir.c_str(), script_dir.c_str()};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_FAILURE, DiscoverAndRunTests(4, argv, {}, &stopwatch, ""));

  // Prepare the expected output.
  fbl::String success_output_rel_path;
  ASSERT_TRUE(GetOutputFileRelPath(output_dir, succeed_file_name, &success_output_rel_path));
  fbl::String failure_output_rel_path;
  ASSERT_TRUE(GetOutputFileRelPath(output_dir, fail_file_name, &failure_output_rel_path));

  char expected_pass_output_buf[1024];
  sprintf(expected_pass_output_buf,
          R"(    \{
      "name": "%s",
      "output_file": "%s",
      "result": "PASS",
      "duration_milliseconds": \d+
    \})",
          succeed_file_name.c_str(),
          success_output_rel_path.c_str() + 1);  // +1 to discard the leading slash.
  std::regex expected_pass_output_regex(expected_pass_output_buf);

  char expected_fail_output_buf[1024];
  sprintf(expected_fail_output_buf,
          R"(    \{
      "name": "%s",
      "output_file": "%s",
      "result": "FAIL",
      "duration_milliseconds": \d+
    \})",
          fail_file_name.c_str(),
          failure_output_rel_path.c_str() + 1);  // +1 to discared the leading slash.
  std::regex expected_fail_output_regex(expected_fail_output_buf);

  // Extract the actual output.
  const fbl::String output_path = JoinPath(output_dir, "summary.json");
  FILE* output_file = fopen(output_path.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);

  // The order of the tests in summary.json is not defined, so first check the
  // prefix, then be permissive about order of the actual tests.
  EXPECT_EQ(0, strncmp(kExpectedJSONOutputPrefix, &buf[0], kExpectedJSONOutputPrefixSize));

  std::cmatch fail_output_match;
  std::cmatch pass_output_match;

  EXPECT_TRUE(std::regex_search(buf, pass_output_match, expected_pass_output_regex));
  EXPECT_TRUE(std::regex_search(buf, fail_output_match, expected_fail_output_regex));

  auto outputs_end_index = std::max(pass_output_match.position() + pass_output_match.length(),
                                    fail_output_match.position() + fail_output_match.length());
  EXPECT_STR_EQ("\n  ]\n}\n", &buf[outputs_end_index]);

  END_TEST;
}

// Passing an -o argument *and* a syslog file name should result in output being
// written that includes a syslog reference.
bool DiscoverAndRunTestsWithSyslogOutput() {
  BEGIN_TEST;

  fbl::String all_scripts_dir = packaged_script_dir();
  fbl::String script_dir = JoinPath(all_scripts_dir, "testwithoutput");

  ScopedTestDir test_dir;
  const fbl::String succeed_file_name = JoinPath(script_dir, "test-with-output-succeed.sh");
  const fbl::String fail_file_name = JoinPath(script_dir, "test-with-output-fail.sh");
  const fbl::String output_dir = JoinPath(test_dir.path(), "run-all-tests-output-2");
  EXPECT_EQ(0, MkDirAll(output_dir));

  const char* const argv[] = {"./runtests", "-o", output_dir.c_str(), script_dir.c_str()};
  TestStopwatch stopwatch;
  EXPECT_EQ(EXIT_FAILURE, DiscoverAndRunTests(4, argv, {}, &stopwatch, "syslog.txt"));

  // Prepare the expected output.
  fbl::String success_output_rel_path;
  ASSERT_TRUE(GetOutputFileRelPath(output_dir, succeed_file_name, &success_output_rel_path));
  fbl::String failure_output_rel_path;
  ASSERT_TRUE(GetOutputFileRelPath(output_dir, fail_file_name, &failure_output_rel_path));

  const char kExpectedOutputsStr[] =
      "  \"outputs\": {\n"
      "    \"syslog_file\": \"syslog.txt\"\n"
      "  }";

  // Extract the actual output.
  const fbl::String output_path = JoinPath(output_dir, "summary.json");
  FILE* output_file = fopen(output_path.c_str(), "r");
  ASSERT_TRUE(output_file);
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
  fclose(output_file);

  // We don't actually care if the string is at the beginning or the end of
  // the JSON, so just search for it anywhere.
  bool found_expected_outputs_str = false;
  for (size_t buf_index = 0; buf[buf_index]; ++buf_index) {
    if (!strncmp(kExpectedOutputsStr, &buf[buf_index], sizeof(kExpectedOutputsStr) - 1)) {
      found_expected_outputs_str = true;
      break;
    }
  }
  if (!found_expected_outputs_str) {
    printf("Unexpected buffer contents: %s\n", buf);
  }
  EXPECT_TRUE(found_expected_outputs_str, "Didn't find expected outputs str in buf");

  END_TEST;
}

BEGIN_TEST_CASE(ParseTestNames)
RUN_TEST(ParseTestNamesEmptyStr)
RUN_TEST(ParseTestNamesEmptyStrInMiddle)
RUN_TEST(ParseTestNamesNormal)
RUN_TEST(ParseTestNamesTrailingComma)
END_TEST_CASE(ParseTestNames)

BEGIN_TEST_CASE(IsInWhitelist)
RUN_TEST(EmptyWhitelist)
RUN_TEST(NonemptyWhitelist)
END_TEST_CASE(IsInWhitelist)

BEGIN_TEST_CASE(JoinPath)
RUN_TEST(JoinPathNoTrailingSlash)
RUN_TEST(JoinPathTrailingSlash)
RUN_TEST(JoinPathAbsoluteChild)
END_TEST_CASE(JoinPath)

BEGIN_TEST_CASE(MkDirAll)
RUN_TEST(MkDirAllTooLong)
RUN_TEST(MkDirAllAlreadyExists)
RUN_TEST(MkDirAllParentAlreadyExists)
RUN_TEST(MkDirAllParentDoesNotExist)
END_TEST_CASE(MkDirAll)

BEGIN_TEST_CASE(WriteSummaryJSON)
RUN_TEST(WriteSummaryJSONSucceeds)
RUN_TEST(WriteSummaryJSONSucceedsWithoutSyslogPath)
RUN_TEST(WriteSummaryJSONBadTestName)
END_TEST_CASE(WriteSummaryJSON)

BEGIN_TEST_CASE(ResolveGlobs)
RUN_TEST(ResolveGlobsNoMatches)
RUN_TEST(ResolveGlobsMultipleMatches)
END_TEST_CASE(ResolveGlobs)

BEGIN_TEST_CASE(RunTest)
RUN_TEST(RunTestSuccess)
RUN_TEST(RunTestTimeout)
RUN_TEST(RunTestSuccessWithStdout)
RUN_TEST(RunTestFailureWithStderr)
RUN_TEST(RunTestFailureToLoadFile)
END_TEST_CASE(RunTest)

BEGIN_TEST_CASE(DiscoverTestsInDirGlobs)
RUN_TEST(DiscoverTestsInDirGlobsBasic)
RUN_TEST(DiscoverTestsInDirGlobsFilter)
RUN_TEST(DiscoverTestsInDirGlobsIgnore)
END_TEST_CASE(DiscoverTestsInDirGlobs)

BEGIN_TEST_CASE(DiscoverTestsInListFile)
RUN_TEST(DiscoverTestsInListFileWithTrailingWhitespace)
END_TEST_CASE(DiscoverTestsInListFile)

BEGIN_TEST_CASE(RunTests)
RUN_TEST(RunTestsCreatesOutputFile)
RUN_TEST_MEDIUM(RunTestsWithVerbosity)
RUN_TEST_MEDIUM(RunTestsWithArguments)
END_TEST_CASE(RunTests)

BEGIN_TEST_CASE(DiscoverAndRunTests)
RUN_TEST_MEDIUM(DiscoverAndRunTestsBasicPass)
RUN_TEST_MEDIUM(DiscoverAndRunTestsBasicFail)
RUN_TEST_MEDIUM(DiscoverAndRunTestsFallsBackToDefaultDirs)
RUN_TEST_MEDIUM(DiscoverAndRunTestsFailsWithNoTestGlobsOrDefaultDirs)
RUN_TEST_MEDIUM(DiscoverAndRunTestsFailsWithBadArgs)
RUN_TEST_MEDIUM(DiscoverAndRunTestsWithGlobs)
RUN_TEST_MEDIUM(DiscoverAndRunTestsWithOutput)
RUN_TEST_MEDIUM(DiscoverAndRunTestsWithSyslogOutput)
END_TEST_CASE(DiscoverAndRunTests)
}  // namespace
}  // namespace runtests

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  if (loop.StartThread() != ZX_OK) {
    fprintf(stderr, "Error: Cannot initialize local memfs loop\n");
    return -1;
  }
  if (memfs_install_at(loop.dispatcher(), runtests::kMemFsRoot) != ZX_OK) {
    fprintf(stderr, "Error: Cannot install local memfs\n");
    return -1;
  }
  return unittest_run_all_tests(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
}
