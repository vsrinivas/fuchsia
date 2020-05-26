// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status.h"

#include <stdio.h>
#include <unistd.h>

#include <string>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

// Trivial create/destroy of the StatusLine class.
TEST(Status, CreateDestroy) { StatusLine trivial{}; }

// Exercise the various commands.
//
// We don't attempt to determine if the output logging is correct.
TEST(Args, BasicLogging) {
  StatusLine status{};

  status.Log("Test string log");
  status.Log("Test format logging %d/%s/%f", 1, "xyz", 3.14);

  status.Set("Set status 1");
  status.Set("Set status 2");
  status.Set("Set status 3");

  status.Log("Test log after status");
  status.Set("Test log after set");
}

}  // namespace
}  // namespace hwstress
