// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/testing/netemul/lib/sync/sync_manager.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  syslog::SetTags({"sync-manager"});
  FX_LOGS(INFO) << "started";

  std::unique_ptr context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  netemul::SyncManager sync_manager;
  context->outgoing()->AddPublicService(sync_manager.GetHandler());
  return loop.Run();
}
