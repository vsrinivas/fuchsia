// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <modular/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/gtest/test_with_loop.h"
#include "peridot/bin/user_runner/puppet_master/puppet_master_impl.h"

namespace modular {
namespace {

class PuppetMasterTest : public gtest::TestWithLoop {};

TEST_F(PuppetMasterTest, Basic) {
  PuppetMasterImpl impl;
  PuppetMasterPtr ptr;
  impl.Connect(ptr.NewRequest());
}

}  // namespace
}  // namespace modular
