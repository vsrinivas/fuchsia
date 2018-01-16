// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_service_impl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"

int main(int argc, const char** argv) {
  fsl::MessageLoop loop;

  std::unique_ptr<app::ApplicationContext> application_context =
      app::ApplicationContext::CreateFromStartupInfo();

  mdns::MdnsServiceImpl impl(application_context.get());

  loop.Run();
  return 0;
}
