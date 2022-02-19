// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/network/mdns/service/services/mdns_service_impl.h"

int main(int argc, const char** argv) {
  syslog::SetTags({"mdns"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  mdns::MdnsServiceImpl impl(component_context.get());

  loop.Run();
  return 0;
}
