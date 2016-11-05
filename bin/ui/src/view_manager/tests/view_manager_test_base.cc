// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/tests/view_manager_test_base.h"

#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "base/run_loop.h"
#include "lib/fidl/cpp/application/application_test_base.h"
#include "lib/fidl/cpp/application/connect.h"

namespace view_manager {
namespace test {

ViewManagerTestBase::ViewManagerTestBase() : weak_factory_(this) {}

ViewManagerTestBase::~ViewManagerTestBase() {}

void ViewManagerTestBase::SetUp() {
  fidl::test::ApplicationTestBase::SetUp();
  quit_message_loop_callback_ =
      base::Bind(&ViewManagerTestBase::QuitMessageLoopCallback,
                 weak_factory_.GetWeakPtr());
}

void ViewManagerTestBase::QuitMessageLoopCallback() {
  base::MessageLoop::current()->Quit();
}

void ViewManagerTestBase::KickMessageLoop() {
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, quit_message_loop_callback_, kDefaultMessageDelay);
  base::MessageLoop::current()->Run();
}

}  // namespace test
}  // namespace view_manager
