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

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <runtests-utils/fuchsia-run-test.h>
#include <runtests-utils/runtests-utils.h>
#include <unittest/unittest.h>

#include "runtests-utils-test-globals.h"
#include "runtests-utils-test-utils.h"

namespace runtests {
namespace {

bool TestFileComponentInfoTest() {
    BEGIN_TEST;
    fbl::String component_url;
    fbl::String cmx_file_path;

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("", &component_url, &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/", &component_url, &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/system/test", &component_url, &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs", &component_url, &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages", &component_url, &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/", &component_url,
                          &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/foo", &component_url,
                          &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/foo/", &component_url,
                          &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/foo/bar", &component_url,
                          &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/foo/bar/", &component_url,
                          &cmx_file_path);
    EXPECT_STR_EQ("", component_url.c_str());
    EXPECT_STR_EQ("", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/", &component_url,
                          &cmx_file_path);
    EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/bar.cmx",
                  component_url.c_str());
    EXPECT_STR_EQ("/pkgfs/packages/pname/meta/bar.cmx", cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/test_file",
                          &component_url, &cmx_file_path);
    EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/test_file.cmx",
                  component_url.c_str());
    EXPECT_STR_EQ("/pkgfs/packages/pname/foo/meta/test_file.cmx",
                  cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/test/file",
                          &component_url, &cmx_file_path);
    EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/file.cmx",
                  component_url.c_str());
    EXPECT_STR_EQ("/pkgfs/packages/pname/foo/bar/meta/file.cmx",
                  cmx_file_path.c_str());

    component_url = "";
    cmx_file_path = "";
    TestFileComponentInfo("/pkgfs/packages/pname/foo/bar/test/file/",
                          &component_url, &cmx_file_path);
    EXPECT_STR_EQ("fuchsia-pkg://fuchsia.com/pname#meta/file.cmx",
                  component_url.c_str());
    EXPECT_STR_EQ("/pkgfs/packages/pname/foo/bar/meta/file.cmx",
                  cmx_file_path.c_str());

    END_TEST;
}

bool RunTestDontPublishData() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    fbl::String test_name = JoinPath(test_dir.path(), "publish-data-helper");
    ScopedTestFile file(test_name.c_str(), "/boot/bin/publish-data-helper");

    const char* argv[] = {test_name.c_str(), nullptr};
    fbl::unique_ptr<Result> result = PlatformRunTest(argv, nullptr, nullptr);
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
    ScopedTestFile file(test_name.c_str(), "/boot/bin/publish-data-helper");
    int num_failed = 0;
    fbl::Vector<fbl::unique_ptr<Result>> results;
    const signed char verbosity = 77;
    const fbl::String output_dir = JoinPath(test_dir.path(), "output");
    const char output_file_base_name[] = "output.txt";
    ASSERT_EQ(0, MkDirAll(output_dir));
    EXPECT_TRUE(RunTests(PlatformRunTest, {test_name},
                         output_dir.c_str(), output_file_base_name, verbosity,
                         &num_failed, &results));
    EXPECT_EQ(0, num_failed);
    EXPECT_EQ(1, results.size());
    EXPECT_LE(1, results[0]->data_sinks.size());

    END_TEST;
}

bool RunAllTestsPublishData() {
    BEGIN_TEST;

    ScopedTestDir test_dir;
    fbl::String test_name = JoinPath(test_dir.path(), "publish-data-helper");
    ScopedTestFile file(test_name.c_str(), "/boot/bin/publish-data-helper");

    const fbl::String output_dir =
        JoinPath(test_dir.path(), "run-all-tests-output-1");
    EXPECT_EQ(0, MkDirAll(output_dir));

    const char* const argv[] = {"./runtests", "-o", output_dir.c_str(),
      test_dir.path()};
    TestStopwatch stopwatch;
    EXPECT_EQ(EXIT_SUCCESS, DiscoverAndRunTests(PlatformRunTest, 4, argv, {},
                                                &stopwatch, ""));

    // Prepare the expected output.
    fbl::String test_output_rel_path;
    ASSERT_TRUE(GetOutputFileRelPath(output_dir, test_name,
                                     &test_output_rel_path));

    fbl::StringBuffer<1024> expected_output_buf;
    expected_output_buf.AppendPrintf(
        "\"name\":\"%s\",\"output_file\":\"%s\",\"result\":\"PASS\"",
        test_name.c_str(),
        test_output_rel_path.c_str() + 1); // +1 to discard the leading slash.

    fbl::String test_data_sink_rel_path;
    ASSERT_TRUE(GetOutputFileRelPath(output_dir, JoinPath(test_name, "test"),
                                     &test_data_sink_rel_path));

    fbl::StringBuffer<1024> expected_data_sink_buf;
    expected_data_sink_buf.AppendPrintf(
        "\"test\":[{\"name\":\"test\",\"file\":\"%s\"}]",
        test_data_sink_rel_path.c_str() + 1); // +1 to discard the leading slash.

    // Extract the actual output.
    const fbl::String output_path = JoinPath(output_dir, "summary.json");
    FILE* output_file = fopen(output_path.c_str(), "r");
    ASSERT_TRUE(output_file);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    EXPECT_LT(0, fread(buf, sizeof(buf[0]), sizeof(buf), output_file));
    fclose(output_file);

    EXPECT_NONNULL(strstr(buf, expected_output_buf.c_str()), "");
    EXPECT_NONNULL(strstr(buf, expected_data_sink_buf.c_str()), "");

    END_TEST;
}

BEGIN_TEST_CASE(FuchsiaComponentInfo)
RUN_TEST_SMALL(TestFileComponentInfoTest)
END_TEST_CASE(FuchsiaComponentInfo)

BEGIN_TEST_CASE(PublishDataTests)
RUN_TEST(RunTestDontPublishData)
RUN_TEST_MEDIUM(RunTestsPublishData)
RUN_TEST_MEDIUM(RunAllTestsPublishData)
END_TEST_CASE(PublishDataTests)

} // namespace
} // namespace runtests
