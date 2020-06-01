// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fs_test_fixture.h"

namespace fs_test {
namespace {

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, FileSystemTest, testing::ValuesIn(AllTestFileSystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
