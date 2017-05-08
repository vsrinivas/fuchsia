// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/tests/composer_test.h"
#include "apps/mozart/src/composer/tests/session_helpers.h"
#include "gtest/gtest.h"
#include "lib/ftl/synchronization/waitable_event.h"

namespace mozart {
namespace composer {
namespace test {

TEST_F(ComposerTest, CreateAndDestroySession) {
  mozart2::SessionPtr session;
  EXPECT_EQ(0U, composer_impl_->GetSessionCount());
  composer_->CreateSession(session.NewRequest());
  RUN_MESSAGE_LOOP_WHILE(composer_impl_->GetSessionCount() != 1);
  session = nullptr;
  RUN_MESSAGE_LOOP_WHILE(composer_impl_->GetSessionCount() != 0);
}

}  // namespace test
}  // namespace composer
}  // namespace mozart
