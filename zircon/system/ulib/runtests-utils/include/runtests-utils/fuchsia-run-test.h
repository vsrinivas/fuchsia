// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTESTS_UTILS_FUCHSIA_RUN_TEST_H_
#define RUNTESTS_UTILS_FUCHSIA_RUN_TEST_H_

#include <stdio.h>

#include <memory>

#include <fbl/string.h>
#include <runtests-utils/runtests-utils.h>

namespace runtests {

struct ComponentInfo {
  fbl::String component_url;
  fbl::String manifest_path;
};

// If tests are in this path, they can run as a component if corresponding cmx
// file is present.
//
// https://fuchsia.dev/fuchsia-src/concepts/storage/package_metadata#component_manifest
constexpr char kPkgPrefix[] = "/pkgfs/packages/";

// If |path| starts with |kPkgPrefix|, this function will generate corresponding
// cmx file path and component url.
//
// if test binary path is: /pkgfs/packages/my_tests/0/test/test_binary, the cmx path
// would be: /pkgfs/packages/my_tests/0/meta/test_binary.cmx
//
// component_url for above path would be:
// fuchsia-pkg://fuchsia.com/my_tests#meta/test_binary.cmx
//
// Code which uses this url:
// https://fuchsia.googlesource.com/fuchsia/+/master/src/sys/appmgr/root_loader.cc
//
void TestFileComponentInfo(const fbl::String& path, ComponentInfo* v1_info_out,
                           ComponentInfo* v2_info_out);

// Invokes a Fuchsia test binary and writes its output to a file.
//
// |argv| is a null-terminated array of argument strings passed to the test
//   program.
// |output_dir| is the name of a directory where debug data
//   will be written. If nullptr, no debug data will be collected.
// |output_filename| is the name of the file to which the test binary's output
//   will be written. May be nullptr, in which case the output will not be
//   redirected.
// |test_name| is used to populate Result properly and in log messages.
// |timeout_millis| is a number of milliseconds to wait for the test. If 0,
//   will wait indefinitely.
std::unique_ptr<Result> FuchsiaRunTest(const char* argv[], const char* output_dir,
                                       const char* output_filename, const char* test_name,
                                       uint64_t timeout_millis);

}  // namespace runtests

#endif  // RUNTESTS_UTILS_FUCHSIA_RUN_TEST_H_
