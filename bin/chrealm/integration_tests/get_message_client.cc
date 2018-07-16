// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/chrealm/cpp/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zx/channel.h>

#include "lib/app/cpp/environment_services.h"
#include "lib/svc/cpp/services.h"

int main(int argc, const char** argv) {
  if (argc != 1) {
    fprintf(stderr, "Usage: %s", argv[0]);
    return 1;
  }

  zx::channel local;
  zx::channel remote;
  zx_status_t status = zx::channel::create(0u, &local, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "Creating channel failed: %s\n",
            zx_status_get_string(status));
    return 1;
  }

  fuchsia::sys::EnvironmentSyncPtr env;
  fuchsia::sys::ServiceProviderSyncPtr svc;
  fuchsia::sys::ConnectToEnvironmentService(env.NewRequest());
  status = env->GetServices(svc.NewRequest());
  if (status != ZX_OK) {
    fprintf(stderr, "Getting services failed: %s\n",
            zx_status_get_string(status));
    return 1;
  }
  status = svc->ConnectToService(fuchsia::testing::chrealm::TestService::Name_,
                                 std::move(remote));
  if (status != ZX_OK) {
    fprintf(stderr, "Connecting to service failed: %s\n",
            zx_status_get_string(status));
    return 1;
  }

  fuchsia::testing::chrealm::TestServiceSyncPtr test_svc;
  test_svc.Bind(std::move(local));
  fidl::StringPtr msg;
  test_svc->GetMessage(&msg);
  printf("%s", msg->c_str());

  return 0;
}
