// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/cli/list.h"

#include <fuchsia/cpp/guest.h>
#include <iostream>

#include "lib/app/cpp/environment_services.h"
#include "lib/fsl/tasks/message_loop.h"

void handle_list() {
  guest::GuestManagerSyncPtr guestmgr;
  component::ConnectToEnvironmentService(guestmgr.NewRequest());
  fidl::VectorPtr<guest::GuestInfo> infos;
  guestmgr->ListGuests(&infos);

  for (const auto& info : *infos) {
    std::cout << info.id << "|    " << info.label << std::endl;
  }
  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}
