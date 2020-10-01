// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/intl/intl_services/run.h"

int main(int argc, const char** argv) {
  const zx_status_t status = intl::serve_intl_profile_provider(argc, argv);
  FX_LOGS(INFO) << "Terminated with status: " << zx_status_get_string(status);
  exit(status);
}
