// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/mdns/service/mdns_service_impl.h"
#include "lib/app/cpp/application_context.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  std::unique_ptr<component::ApplicationContext> application_context =
      component::ApplicationContext::CreateFromStartupInfo();

  mdns::MdnsServiceImpl impl(application_context.get());

  loop.Run();
  return 0;
}
