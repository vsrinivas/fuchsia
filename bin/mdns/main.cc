// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/mdns_impl.h"
#include "garnet/bin/mdns/mdns_params.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/netconnector/fidl/netconnector.fidl.h"

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  mdns::MdnsParams params(command_line);
  if (!params.is_valid()) {
    return 1;
  }

  fsl::MessageLoop loop;

  std::unique_ptr<app::ApplicationContext> application_context =
      app::ApplicationContext::CreateFromStartupInfo();

  mdns::MdnsImpl impl(application_context.get(), &params);

  loop.Run();
  return 0;
}
