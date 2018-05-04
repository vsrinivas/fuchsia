// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/service.h"

#include <iostream>

#include <fuchsia/cpp/guest.h>

#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"

// Pointer to the controller for the guest.
guest::GuestControllerPtr g_guest_controller;

guest::GuestController* connect(uint32_t env_id, uint32_t cid) {
  guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());

  fidl::VectorPtr<guest::GuestEnvironmentInfo> env_infos;
  guest::GuestEnvironmentSyncPtr env_ptr;
  guestmgr->ConnectToEnvironment(env_id, env_ptr.NewRequest());

  env_ptr->ConnectToGuest(cid, g_guest_controller.NewRequest());
  g_guest_controller.set_error_handler(
      [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  return g_guest_controller.get();
}
