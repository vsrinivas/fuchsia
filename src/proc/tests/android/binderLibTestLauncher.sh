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

GTEST_FILTER=""

# Basic transaction tests.
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.NopTransaction"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.NopTransactionOneway"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.NopTransactionClear"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.SetError"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.GetId"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.IndirectGetId2"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.IndirectGetId3"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.Callback"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.AddServer"

# Death notification tests.
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.DeathNotificationStrongRef"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.DeathNotificationMultiple"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.DeathNotificationThread"

# File tests.
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.PassFile"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.PassParcelFileDescriptor"

# Worksource tests.
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WorkSourceUnsetByDefault"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WorkSourceSet"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WorkSourceSetWithoutPropagation"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WorkSourceCleared"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WorkSourceRestored"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.PropagateFlagSet"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.PropagateFlagCleared"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.PropagateFlagRestored"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WorkSourcePropagatedForAllFollowingBinderCalls"

# Misc tests.
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WasParceled"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.PtrSize"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.PromoteLocal"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.LocalGetExtension"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.RemoteGetExtension"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.CheckHandleZeroBinderHighBitsZeroCookie"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.FreedBinder"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.BufRejected"
GTEST_FILTER="$GTEST_FILTER:BinderLibTest.WeakRejected"

# Start the actual test.
/vendor/data/nativetest64/binderLibTest/binderLibTest "--gtest_filter=${GTEST_FILTER}"
