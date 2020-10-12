// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/kernel/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <stdio.h>

const char kRootJobSvc[] = "/svc/" fuchsia_kernel_RootJob_Name;

zx::job GetRootJob() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    fprintf(stderr, "unable to create channel\n");
    return zx::job();
  }

  status = fdio_service_connect(kRootJobSvc, remote.release());
  if (status != ZX_OK) {
    fprintf(stderr, "unable to open fuchsia.kernel.RootJob channel\n");
    return zx::job();
  }

  zx::job root_job;
  zx_status_t fidl_status =
      fuchsia_kernel_RootJobGet(local.get(), root_job.reset_and_get_address());
  if (fidl_status != ZX_OK) {
    fprintf(stderr, "unable to get root job %d\n", fidl_status);
    return zx::job();
  }

  return root_job;
}
