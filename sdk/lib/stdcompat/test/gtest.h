// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// While we guarantee that stdcompat compiles with -Wundef, we cannot assure the same
// for gtest.
#ifdef __clang__

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundef"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#pragma clang diagnostic pop

#elif __GNUC__

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

#else

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#endif
