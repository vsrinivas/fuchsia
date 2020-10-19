// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/recovery/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

// A simplie command-line tool for initiating factory reset.
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fuchsia::recovery::FactoryResetSyncPtr fdr;
  context->svc()->Connect(fdr.NewRequest());
  zx_status_t status, call_status;
  status = fdr->Reset(&call_status);
  if (status != ZX_OK) {
    if (status == ZX_ERR_PEER_CLOSED) {
      // "/svc/fuchsia.recovery.FactoryReset" may not be available if the cli
      // is run from the serial console which does not depend on appmgr.
      fprintf(stderr,
              "If you're running this from the serial console, "
              "that's unsupported -- try again from fx shell.\n");
    }
    return status;
  }
  return call_status;
}
