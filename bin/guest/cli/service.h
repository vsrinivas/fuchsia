// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_SERVICE_H_
#define GARNET_BIN_GUEST_CLI_SERVICE_H_

#include <iostream>

#include <fuchsia/cpp/guest.h>

#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"

// Pointer to the controller for the guest.
extern guest::GuestControllerPtr g_guest_controller;

static inline guest::GuestController* connect(uint32_t guest_id) {
  guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  guestmgr->Connect(guest_id, g_guest_controller.NewRequest());
  g_guest_controller.set_error_handler(
      [] { fsl::MessageLoop::GetCurrent()->PostQuitTask(); });
  return g_guest_controller.get();
}

#endif  // GARNET_BIN_GUEST_CLI_SERVICE_H_
