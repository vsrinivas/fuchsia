// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.exampletester/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <tools/fidl/example-tester/example/cpp_wire/client/config.h>

int main(int argc, const char** argv) {
  FX_LOGS(INFO) << "Started";

  // Retrieve component configuration.
  auto conf = config::Config::TakeFromStartupHandle();

  // Only try to contact the server if instructed - if not, do the calculation locally instead.
  if (conf.do_in_process()) {
    FX_LOGS(INFO) << "Response: " << conf.augend() + conf.addend();
  } else {
    // Connect to the protocol inside the component's namespace. This can fail so it's wrapped in a
    // |zx::status| and it must be checked for errors.
    zx::status client_end = component::Connect<test_exampletester::Simple>();
    if (!client_end.is_ok()) {
      FX_LOGS(ERROR) << "Synchronous error when connecting to the |Simple| protocol: "
                     << client_end.status_string();
      return -1;
    }

    // Create a synchronous client using the newly-established connection.
    fidl::WireSyncClient client(std::move(*client_end));
    FX_LOGS(INFO) << "Outgoing connection enabled";

    // Make the FIDL call.
    fidl::WireResult result = client->Add(conf.augend(), conf.addend());

    // Check if the FIDL call succeeded or not.
    if (!result.ok()) {
      // If the call failed, log the error, and quit the program. Production code should do more
      // graceful error handling depending on the situation.
      FX_LOGS(ERROR) << "Add failed: " << result.error();
      return -1;
    }

    FX_LOGS(INFO) << "Response: " << result.value().sum;
  }

  // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
  // referenced bug has been resolved, we can remove the sleep.
  sleep(2);
  return 0;
}
