// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "apps/mozart/src/composer/composer_impl.h"

#include "gtest/gtest.h"

namespace mozart {
namespace composer {
namespace test {

class ComposerTest : public mozart::test::TestWithMessageLoop {
 public:
  // ::testing::Test virtual method.
  void SetUp() override;

  // ::testing::Test virtual method.
  void TearDown() override;

  SessionPtr NewSession();

 protected:
  mozart2::ComposerPtr composer_;
  std::unique_ptr<fidl::Binding<mozart2::Composer>> composer_binding_;
  std::unique_ptr<ComposerImpl> composer_impl_;
  std::unique_ptr<mtl::Thread> thread_;
};

}  // namespace test
}  // namespace composer
}  // namespace mozart
