#!/bin/bash
# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This creates versions of the before and after tests that refer to the during
# FIDL library.

set -eu

cd "$( dirname "${BASH_SOURCE[0]}" )"

BEFORE_REGEX='s,fidl/fidl/test/before,fidl/fidl/test/during,g;s/before_/before_during_/g;s/before\./during./g;s/_before/_before_during/g'
AFTER_REGEX='s,fidl/fidl/test/after,fidl/fidl/test/during,g;s/after_/after_during_/g;s/after\./during./g;s/_after/_after_during/g'

sed -e "$BEFORE_REGEX" server_1_before.go > server_2_before_during.go
sed -e "$AFTER_REGEX" server_4_after.go > server_3_after_during.go

sed -e "$BEFORE_REGEX" client_1_before.go > client_2_before_during.go
sed -e "$AFTER_REGEX" client_4_after.go > client_3_after_during.go
