// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/lifecycle.h"

#include <lib/fidl-async/cpp/bind.h>

#include "src/sys/appmgr/appmgr.h"

namespace component {

zx_status_t LifecycleServer::Create(async_dispatcher_t* dispatcher, zx::channel channel) {
  lifecycle_ = fidl::BindServer(dispatcher, std::move(channel), this);
  return ZX_OK;
}

void LifecycleServer::Close(zx_status_t status) {
  FX_LOGS(INFO) << "Closing appmgr lifecycle channel.";
  if (lifecycle_) {
    lifecycle_->Close(status);
  } else {
    FX_LOGS(ERROR) << "Appmgr lifecycle not bound.";
  }
}

void LifecycleServer::Stop(StopRequestView request, StopCompleter::Sync& completer) {
  FX_LOGS(INFO) << "appmgr: received shutdown command over lifecycle interface";
  child_lifecycles_ = appmgr_->Shutdown([this](zx_status_t status) mutable {
    FX_LOGS(INFO) << "Lifecycle Server complete callback";
    child_lifecycles_.clear();
    Close(status);
    stop_callback_(status);
  });
}

}  // namespace component
