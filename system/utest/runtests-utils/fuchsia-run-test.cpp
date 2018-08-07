// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <runtests-utils/fuchsia-run-test.h>
#include <unittest/unittest.h>

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

BEGIN_TEST_CASE(FuchsiaComponentInfo)
RUN_TEST_SMALL(TestFileComponentInfoTest)
END_TEST_CASE(FuchsiaComponentInfo)

} // namespace
} // namespace runtests
