// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_APPMGR_STARTUP_SERVICE_H_
#define SRC_SYS_APPMGR_STARTUP_SERVICE_H_

#include <fuchsia/appmgr/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/zx/channel.h>

class StartupServiceImpl final : public fuchsia::appmgr::Startup {
 public:
  StartupServiceImpl() = default;

  zx_status_t Bind(async_dispatcher_t* dispatcher,
                   fidl::InterfaceRequest<fuchsia::appmgr::Startup> req);
  void LaunchAppmgr() override;

 private:
  fidl::BindingSet<fuchsia::appmgr::Startup> bindings_;
};

#endif
