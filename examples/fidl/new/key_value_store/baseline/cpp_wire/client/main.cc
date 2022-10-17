// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples.keyvaluestore.baseline/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <examples/fidl/new/key_value_store/baseline/cpp_wire/client/config.h>
#include <src/lib/files/file.h>
#include <src/lib/fxl/strings/string_printf.h>

int main(int argc, const char** argv) {
  FX_LOGS(INFO) << "Started";

  // Retrieve component configuration.
  auto conf = config::Config::TakeFromStartupHandle();

  // Start up an async loop and dispatcher.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Connect to the protocol inside the component's namespace. This can fail so it's wrapped in a
  // |zx::result| and it must be checked for errors.
  zx::result client_end = component::Connect<examples_keyvaluestore_baseline::Store>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Store| protocol: "
                   << client_end.status_string();
    return -1;
  }

  // Create an asynchronous client using the newly-established connection.
  fidl::WireClient client(std::move(*client_end), dispatcher);
  FX_LOGS(INFO) << "Outgoing connection enabled";

  for (const auto& key : conf.write_items()) {
    std::string text;
    if (!files::ReadFileToString(fxl::StringPrintf("/pkg/data/%s.txt", key.c_str()), &text)) {
      FX_LOGS(ERROR) << "It looks like the correct `resource` dependency has not been packaged";
      break;
    }

    auto value = std::vector<uint8_t>(text.begin(), text.end());
    client
        ->WriteItem(
            {fidl::StringView::FromExternal(key), fidl::VectorView<uint8_t>::FromExternal(value)})
        .ThenExactlyOnce(
            [&](fidl::WireUnownedResult<examples_keyvaluestore_baseline::Store::WriteItem>&
                    result) {
              if (!result.ok()) {
                FX_LOGS(ERROR) << "Unexpected framework error";
              } else if (result->is_error()) {
                FX_LOGS(INFO) << "WriteItem Error: " << fidl::ToUnderlying(result->error_value());
              } else {
                FX_LOGS(INFO) << "WriteItem Success";
              }

              // Quit the loop, thereby handing control back to the outer loop of actions being
              // iterated over.
              loop.Quit();
            });

    // Run the loop until the callback is resolved, at which point we can continue from here.
    loop.Run();
    loop.ResetQuit();
  }

  // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
  // referenced bug has been resolved, we can remove the sleep.
  sleep(2);
  return 0;
}
