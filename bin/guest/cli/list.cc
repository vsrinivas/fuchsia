// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/list.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <iostream>

#include "lib/app/cpp/environment_services.h"

void handle_list() {
  fuchsia::guest::GuestManagerSync2Ptr guestmgr;
  fuchsia::sys::ConnectToEnvironmentService(guestmgr.NewRequest());
  fidl::VectorPtr<fuchsia::guest::GuestEnvironmentInfo> env_infos;
  guestmgr->ListEnvironments(&env_infos);

  for (const auto& env_info : *env_infos) {
    printf("env:%-4u          %s\n", env_info.id, env_info.label->c_str());
    for (const auto& guest_info : *env_info.guests) {
      printf(" guest:%-4u       %s\n", guest_info.cid,
             guest_info.label->c_str());
    }
  }
}
