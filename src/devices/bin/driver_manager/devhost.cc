// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost.h"

#include "coordinator.h"
#include "log.h"

Devhost::Devhost(Coordinator* coordinator) : coordinator_(coordinator) {
  coordinator_->RegisterDevhost(this);
}

Devhost::~Devhost() {
  log(INFO, "driver_manager: destroy host %p\n", this);
  coordinator_->UnregisterDevhost(this);
  zx_handle_close(hrpc_);
  proc_.kill();
}
