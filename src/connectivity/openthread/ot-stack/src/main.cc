// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>

#include "app.h"

void runThirdPartyFunc();

// Usage: accept argument "real_device_path" or "-t mock_device_path"
int main(int argc, const char** argv) {
  runThirdPartyFunc();
  otstack::OtStackApp app;

  syslog::InitLogger({"ot-stack"});

  if (argc < 2) {
    FX_LOGS(ERROR) << "invalid number of arguments provided\n";
    return ZX_ERR_INVALID_ARGS;
  }

  bool is_test_env = false;
  std::string path = argv[1];
  if (path.compare("-t") == 0) {
    is_test_env = true;
    if (argc < 3) {
      FX_LOGS(ERROR) << "no path provided\n";
      return ZX_ERR_INVALID_ARGS;
    }
    path = argv[2];
  }

  if (path.find("class/ot-radio") == std::string::npos) {
    FX_LOGS(ERROR) << "invalid path provided\n";
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t res = app.Init(path, is_test_env);
  FX_PLOGS(INFO, res) << "init returned, starting loop";

  return app.loop()->Run() != ZX_OK;
}
