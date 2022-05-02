// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/debugdata/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>

#include "lib/zx/eventpair.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto env_services = sys::ServiceDirectory::CreateFromNamespace();
  fuchsia::debugdata::PublisherPtr ptr;
  env_services->Connect(ptr.NewRequest());
  zx::vmo vmo;
  zx::vmo::create(1024, 0, &vmo);
  zx::eventpair t1, t2;
  zx::eventpair::create(0, &t1, &t2);
  ptr->Publish("some_name", std::move(vmo), std::move(t1));

  // run until this is killed by caller.
  loop.Run();
  return 0;
}
