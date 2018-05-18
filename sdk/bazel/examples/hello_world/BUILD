# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

licenses(["unencumbered"])

load("//fuchsia:fuchsia.bzl", "fuchsia_package")

cc_binary(
    name = "hello_world",
    srcs = ["hello_world.cc"],
)

fuchsia_package(
	name = "hello_world_package",
	deps = [":hello_world"],
)