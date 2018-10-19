// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_PACKAGE_UPDATING_LOADER_H_
#define GARNET_BIN_SYSMGR_PACKAGE_UPDATING_LOADER_H_

#include <string>

#include <fuchsia/amber/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include "garnet/lib/loader/component_loader.h"
#include "garnet/lib/pkg_url/fuchsia_pkg_url.h"
#include "lib/fxl/macros.h"

namespace sysmgr {

// A component loader that updates a package (or installs it for the first time)
// before running a component in it. Requires a connection to the amber service.
class PackageUpdatingLoader final : public component::ComponentLoader {
 public:
  typedef fit::function<void(std::string)> DoneCallback;

  PackageUpdatingLoader(fuchsia::amber::ControlPtr amber_ctl,
                        async_dispatcher_t* dispatcher);
  ~PackageUpdatingLoader() override;

 private:
  void StartUpdatePackage(const std::string& package_name,
                          DoneCallback done_cb);
  void ListenForPackage(zx::channel reply_chan, DoneCallback done_cb);
  static void WaitForUpdateDone(async_dispatcher_t* dispatcher,
                                async::Wait* wait, zx_status_t status,
                                const zx_packet_signal_t* signal,
                                DoneCallback done_cb);
  static void FinishWaitForUpdate(async_dispatcher_t* dispatcher,
                                  async::Wait* wait, zx_status_t status,
                                  const zx_packet_signal_t* signal,
                                  bool daemon_err,
                                  DoneCallback done_cb);

  bool LoadComponentFromPkgfs(component::FuchsiaPkgUrl component_url,
                              LoadComponentCallback callback) override;

  fuchsia::amber::ControlPtr amber_ctl_;
  async_dispatcher_t* const dispatcher_;  // Not owned.

  FXL_DISALLOW_COPY_AND_ASSIGN(PackageUpdatingLoader);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_PACKAGE_UPDATING_LOADER_H_
