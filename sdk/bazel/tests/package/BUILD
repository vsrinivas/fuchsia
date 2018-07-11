# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("@fuchsia_sdk//build_defs:package.bzl", "fuchsia_package")
load("@fuchsia_sdk//build_defs:package_files.bzl", "package_files")

package_files(
    name = "files",
    contents = {
        "file1.txt": "somedir/file.txt",
        "file2.txt": "base.txt",
    }
)

fuchsia_package(
    name = "package",
    deps = [
        ":files",
    ],
)
