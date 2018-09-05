// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/list.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <iostream>

void handle_list(component::StartupContext* context) {
  fuchsia::guest::EnvironmentManagerSyncPtr guestmgr;
  context->ConnectToEnvironmentService(guestmgr.NewRequest());
  fidl::VectorPtr<fuchsia::guest::EnvironmentInfo> env_infos;
  guestmgr->List(&env_infos);

  for (const auto& env_info : *env_infos) {
    printf("env:%-4u          %s\n", env_info.id, env_info.label->c_str());
    for (const auto& guest_info : *env_info.instances) {
      printf(" guest:%-4u       %s\n", guest_info.cid,
             guest_info.label->c_str());
    }
  }
}
