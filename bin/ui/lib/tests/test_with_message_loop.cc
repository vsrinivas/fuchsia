// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/tests/test_with_message_loop.h"

#include "lib/ftl/functional/make_copyable.h"

namespace mozart {
namespace test {

bool RunLoopWithTimeout(ftl::TimeDelta timeout) {
  auto canceled = std::make_unique<bool>(false);
  bool* canceled_ptr = canceled.get();
  bool timed_out = false;
  mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
      ftl::MakeCopyable([ canceled = std::move(canceled), &timed_out ] {
        if (*canceled) {
          return;
        }
        timed_out = true;
        mtl::MessageLoop::GetCurrent()->QuitNow();
      }),
      timeout);
  mtl::MessageLoop::GetCurrent()->Run();
  if (!timed_out) {
    *canceled_ptr = true;
  }
  return timed_out;
}

}  // namespace test
}  // namespace mozart
