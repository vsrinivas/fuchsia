// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

#include <memory>

#include "fuchsia/kernel/cpp/fidl.h"
#include "lib/async-loop/cpp/loop.h"
#include "lib/async-loop/default.h"
#include "lib/sys/cpp/component_context.h"
#include "src/developer/kcounter_inspect/vmo_file_with_update.h"

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  fuchsia::kernel::CounterSyncPtr kcounter;
  ZX_ASSERT(context->svc()->Connect<fuchsia::kernel::Counter>(kcounter.NewRequest()) == ZX_OK);

  fuchsia::mem::Buffer buffer;
  zx_status_t status;
  ZX_ASSERT(kcounter->GetInspectVmo(&status, &buffer) == ZX_OK);
  ZX_ASSERT(status == ZX_OK);

  auto vmo_file =
      std::make_unique<VmoFileWithUpdate>(std::move(buffer.vmo), 0, buffer.size, &kcounter);
  vfs::PseudoDir* dir = context->outgoing()->GetOrCreateDirectory("diagnostics");
  dir->AddEntry("root.inspect", std::move(vmo_file));
  loop.Run();

  return 0;
}
