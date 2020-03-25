// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "gtest/gtest.h"

TEST(LongRunningTest, LongRunning) { sleep(10); }
