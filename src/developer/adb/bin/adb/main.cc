// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "adb.h"

int main(int argc, char** argv) {
  syslog::SetTags({"adb"});

  async::Loop loop{&kAsyncLoopConfigNeverAttachToThread};
  auto status = loop.StartThread("adb-thread");
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Could not start loop";
    return status;
  }

  auto adb = adb::Adb::Create(loop.dispatcher());
  if (adb.is_error()) {
    FX_LOGS(ERROR) << "Could not create adb " << adb.error_value();
    return adb.error_value();
  }

  loop.JoinThreads();
  return ZX_OK;
}
