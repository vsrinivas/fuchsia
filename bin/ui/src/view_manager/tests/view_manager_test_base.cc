// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/tests/view_manager_test_base.h"

#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/services/views/interfaces/views.mojom.h"
#include "base/run_loop.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"

namespace view_manager {
namespace test {

ViewManagerTestBase::ViewManagerTestBase() : weak_factory_(this) {}

ViewManagerTestBase::~ViewManagerTestBase() {}

void ViewManagerTestBase::SetUp() {
  mojo::test::ApplicationTestBase::SetUp();
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
