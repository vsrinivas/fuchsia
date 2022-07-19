// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/inspect/component/cpp/component.h>

#include "lib/sys/component/cpp/outgoing_directory.h"

using inspect::ComponentInspector;

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto* dispatcher = loop.dispatcher();
  auto out = component::OutgoingDirectory::Create(dispatcher);
  auto inspector = ComponentInspector(out, dispatcher);

  inspector.root().RecordInt("val1", 1);
  inspector.root().RecordInt("val2", 2);
  inspector.root().RecordInt("val3", 3);
  inspector.root().RecordLazyNode("child", [] {
    inspect::Inspector insp;
    insp.GetRoot().CreateInt("val", 0, &insp);
    return fpromise::make_ok_promise(std::move(insp));
  });
  inspector.root().RecordLazyValues("values", [] {
    inspect::Inspector insp;
    insp.GetRoot().CreateInt("val4", 4, &insp);
    return fpromise::make_ok_promise(std::move(insp));
  });

  if (out.ServeFromStartupInfo().is_error()) {
    return -1;
  }

  inspector.Health().Ok();

  loop.Run();
  return 0;
}
