// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/glue/test/run_loop.h"

#include "lib/mtl/tasks/message_loop.h"

namespace glue {
namespace test {

void QuitLoop() {
  mtl::MessageLoop::GetCurrent()->QuitNow();
}

void RunLoop() {
  mtl::MessageLoop::GetCurrent()->Run();
}

}  // namespace test
}  // namespace glue
