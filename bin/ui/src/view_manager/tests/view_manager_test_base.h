// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/bind.h"
#include "mojo/public/cpp/application/application_test_base.h"

namespace view_manager {
namespace test {

const base::TimeDelta kDefaultMessageDelay =
    base::TimeDelta::FromMilliseconds(10);

// Run message loop until condition is true (timeout after 400*10ms = 4000ms)
#define KICK_MESSAGE_LOOP_WHILE(x)     \
  for (int i = 0; x && i < 400; i++) { \
    KickMessageLoop();                 \
  }

class ViewManagerTestBase : public mojo::test::ApplicationTestBase {
 public:
  ViewManagerTestBase();
  ~ViewManagerTestBase() override;

  void SetUp() override;
  void KickMessageLoop();

 protected:
  base::Closure quit_message_loop_callback_;
  base::WeakPtrFactory<ViewManagerTestBase> weak_factory_;

 private:
  void QuitMessageLoopCallback();

  DISALLOW_COPY_AND_ASSIGN(ViewManagerTestBase);
};

}  // namespace test
}  // namespace view_manager
