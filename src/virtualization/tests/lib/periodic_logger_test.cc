// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "periodic_logger.h"

#include <gtest/gtest.h>

// Ensure we can safely start and stop the logger.
//
// We don't attempt to verify that logs were written.
TEST(PeriodicLogger, StartStop) {
  PeriodicLogger logger;
  logger.Stop();
  logger.Start("hello, world", zx::sec(1));
  logger.Start("goodbye, world", zx::sec(1));
  logger.Stop();
  logger.Stop();
}
