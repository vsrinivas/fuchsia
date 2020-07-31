// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_LIFECYCLE_H_
#define SRC_SYS_APPMGR_LIFECYCLE_H_

#include <fuchsia/process/lifecycle/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/channel.h>

namespace component {

class Appmgr;

class LifecycleServer final : public llcpp::fuchsia::process::lifecycle::Lifecycle::Interface {
 public:
  LifecycleServer(Appmgr* appmgr) : appmgr_(appmgr) {}

  zx_status_t Create(async_dispatcher_t* dispatcher, zx::channel chan);
  void Close(zx_status_t status);

  static zx_status_t Create(async_dispatcher_t* dispatcher, Appmgr* appmgr, zx::channel channel);

  void Stop(StopCompleter::Sync completer) override;

 private:
  Appmgr* appmgr_;
  std::optional<fidl::ServerBindingRef<llcpp::fuchsia::process::lifecycle::Lifecycle>> lifecycle_;
};

}  // namespace component

#endif  // SRC_SYS_APPMGR_LIFECYCLE_H_
