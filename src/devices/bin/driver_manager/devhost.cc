// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devhost.h"

#include "coordinator.h"
#include "log.h"

Devhost::Devhost(Coordinator* coordinator, zx::channel rpc)
    : coordinator_(coordinator), hrpc_(std::move(rpc)) {
  coordinator_->RegisterDevhost(this);
}

Devhost::~Devhost() {
  log(INFO, "driver_manager: destroy host %p\n", this);
  coordinator_->UnregisterDevhost(this);
  proc_.kill();
}
