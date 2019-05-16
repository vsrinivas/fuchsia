// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/recovery/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

// A simplie command-line tool for initiating factory reset.
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = sys::ComponentContext::Create();
  fuchsia::recovery::FactoryResetSyncPtr fdr;
  context->svc()->Connect(fdr.NewRequest());
  zx_status_t status, call_status;
  status = fdr->Reset(&call_status);
  if (status != ZX_OK) {
    return status;
  }
  return call_status;
}
