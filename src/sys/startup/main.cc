// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/appmgr/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fit/function.h>

#include <fbl/string.h>

/*
 * The startup component exists to start the `appmgr` component that exists
 * inside the `core` realm. This arrangement solves the problem that in some
 * builds `appmgr` is not included. It would be more straightforward to have
 * `appmgr` in a direct `eager` lineage from the root component, but that would
 * cause the system to crash in product configurations that don't include
 * `appmgr` because failure to start an `eager` child is fatal to a parent.
 * startup is not a child of the `core` component because that component is
 * itself stored in pkgfs, which may not be included in the build.
 *
 * startup works by using a capability routed to it from `appmgr`. startup
 * connects to this capability, tries to send a request, and exits. Success or
 * failure of the request is irrelevant, startup is just making the component
 * manager resolve and start `appmgr`, if it is present.
 */

int main() {
  async::Loop loop((async::Loop(&kAsyncLoopConfigAttachToCurrentThread)));
  zx::channel remote;
  zx::channel local;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    exit(-1);
  }
  auto path = fbl::String("/svc/fuchsia.appmgr.Startup");

  status = fdio_service_connect(path.data(), remote.release());
  if (status != ZX_OK) {
    // This failed, presumably appmgr is not available.
    return 0;
  }

  llcpp::fuchsia::appmgr::Startup::SyncClient client(std::move(local));
  client.LaunchAppmgr();
  // We ignore the result here. A failure probably indicates we're running on a
  // config that doesn't include appmgr.
  //
  // In the future we may want to wait indefinitely without exiting if
  // components are required to have an active client to keep them running. As
  // of this writing components are not actively halted if they have no
  // clients, but this may change.
}
