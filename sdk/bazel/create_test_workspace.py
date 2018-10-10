#!/usr/bin/env python
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import argparse
import os
import shutil
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

def create_test_workspace(sdk, output):
    # Remove any existing output.
    shutil.rmtree(output, True)

    shutil.copytree(os.path.join(SCRIPT_DIR, 'tests'), output)

    with open(os.path.join(output, 'WORKSPACE'), 'w') as workspace_file:
        workspace_file.write('''# This is a generated file.

local_repository(
    name = "fuchsia_sdk",
    path = "%s",
)

http_archive(
  name = "io_bazel_rules_dart",
  url = "https://github.com/dart-lang/rules_dart/archive/master.zip",
  strip_prefix = "rules_dart-master",
)

load("@io_bazel_rules_dart//dart/build_rules:repositories.bzl", "dart_repositories")
dart_repositories()

load("@fuchsia_sdk//build_defs:crosstool.bzl", "install_fuchsia_crosstool")
install_fuchsia_crosstool(
    name = "fuchsia_crosstool",
)

load("@fuchsia_sdk//build_defs:setup_dart.bzl", "setup_dart")
setup_dart()

load("@fuchsia_sdk//build_defs:setup_flutter.bzl", "setup_flutter")
setup_flutter()
''' % os.path.relpath(sdk, output))

    return True
