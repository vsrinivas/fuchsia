// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "netboot.h"

#include <stdio.h>
#include <lib/fdio/io.h>

void netboot_run_cmd(const char* cmd) {
  printf("rejecting net cmd [%.6s ..]. This configuration has this feature disabled.\n", cmd);
}
