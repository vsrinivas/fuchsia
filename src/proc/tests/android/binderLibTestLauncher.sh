#!/system/bin/sh
# Copyright 2022 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

set -e

# Start servicemanager in the background, which is the name server for binder objects.
servicemanager &

function cleanup {
    # Kill servicemanager, as it will never exit on its own.
    kill -9 `jobs -p`
}
trap cleanup EXIT

# Start the actual test.
/vendor/data/nativetest64/binderLibTest/binderLibTest "--gtest_filter=BinderLibTest.NopTransaction"
