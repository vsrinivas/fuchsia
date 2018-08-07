// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_FUCHSIA_RUN_TEST_H_
#define ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_FUCHSIA_RUN_TEST_H_

#include <stdio.h>

#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <runtests-utils/runtests-utils.h>

namespace runtests {

// If tests are in this path, they can run as a component if corresponding cmx
// file is present.
//
// https://fuchsia.googlesource.com/docs/+/master/the-book/package_metadata.md#component-manifest
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
// https://fuchsia.googlesource.com/garnet/+/master/bin/appmgr/root_loader.cc
//
void TestFileComponentInfo(const fbl::String path,
                           fbl::String* component_url_out,
                           fbl::String* cmx_file_path_out);

// Invokes a Fuchsia test binary and writes its output to a file.
//
// |argv| is a null-terminated array of argument strings passed to the test
//   program.
// |output_filename| is the name of the file to which the test binary's output
//   will be written. May be nullptr, in which case the output will not be
//   redirected.
fbl::unique_ptr<Result> FuchsiaRunTest(const char* argv[],
                                       const char* output_filename);

} // namespace runtests

#endif // ZIRCON_SYSTEM_ULIB_RUNTESTS_UTILS_INCLUDE_RUNTESTS_UTILS_FUCHSIA_RUN_TEST_H_
