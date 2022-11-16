// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>

#include "adb-file-sync.h"

int main(int argc, char** argv) {
  syslog::SetTags({"adb"});
  std::optional<std::string> default_component;
  if (argc == 1) {
    default_component = argv[0];
  }
  return adb_file_sync::AdbFileSync::StartService(std::move(default_component));
}
