// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "src/virtualization/bin/guest_manager/guest_manager.h"

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  GuestManager manager(loop.dispatcher(), context.get());
  return loop.Run();
}
