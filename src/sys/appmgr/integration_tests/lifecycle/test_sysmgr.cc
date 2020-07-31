// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

int main(int argc, const char** argv) {
  syslog::SetTags({"test_sysmgr"});
  FX_LOGS(INFO) << "Launching TestSysmgr";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  loop.Run();
}
