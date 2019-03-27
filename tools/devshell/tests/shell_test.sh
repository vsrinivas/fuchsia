#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
#
# Usage: shell_test.sh

# Halt on use of undeclared variables and errors.
set -o nounset
set -o errexit

# Choose a directory that won't likely have its name changed or appear in
# in error message.
readonly PKGFS="pkgfs"
readonly RESULT=$(fx shell ls | grep "${PKGFS}")

if [ "${RESULT}" != "${PKGFS}" ]; then
  exit 1
fi

