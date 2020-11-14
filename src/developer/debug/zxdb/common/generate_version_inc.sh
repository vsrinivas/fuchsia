#!/usr/bin/env bash
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# usage: generate_version_inc.sh <file contains version string> <path/to/gen/version.inc>

echo "#define BUILD_VERSION \"$(cat $1)\"" > $2
