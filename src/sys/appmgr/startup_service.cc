// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The startup service implements a very simple protocol. This protocol gives
// things wishing to start appmgr a way to do so via capability routing.
#include "src/sys/appmgr/startup_service.h"

#include <fuchsia/appmgr/cpp/fidl.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/sys/appmgr/appmgr.h"

zx_status_t StartupServiceImpl::Bind(async_dispatcher_t* dispatcher,
                                       fidl::InterfaceRequest<fuchsia::appmgr::Startup> req) {
  bindings_.AddBinding(this, std::move(req), dispatcher, nullptr);
  return ZX_OK;
}

void StartupServiceImpl::LaunchAppmgr() {
  // Nothing to do here. In theory we could close the channel to the client,
  // but this is challenging with BindingSet since this instance is the server
  // for all channels.
}
