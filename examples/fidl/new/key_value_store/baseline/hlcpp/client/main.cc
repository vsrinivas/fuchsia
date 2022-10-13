// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <examples/fidl/new/key_value_store/baseline/hlcpp/client/config.h>
#include <examples/keyvaluestore/baseline/cpp/fidl.h>
#include <src/lib/files/file.h>
#include <src/lib/fxl/strings/string_printf.h>

int main(int argc, const char** argv) {
  FX_LOGS(INFO) << "Started";

  // Retrieve component configuration.
  auto conf = config::Config::TakeFromStartupHandle();

  // Start up an async loop.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Connect to the protocol inside the component's namespace, then create an asynchronous client
  // using the newly-established connection.
  examples::keyvaluestore::baseline::StorePtr store_proxy;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(store_proxy.NewRequest(dispatcher));
  FX_LOGS(INFO) << "Outgoing connection enabled";

  store_proxy.set_error_handler([&loop](zx_status_t status) {
    FX_LOGS(ERROR) << "Shutdown unexpectedly";
    loop.Quit();
  });

  for (const auto& key : conf.write_items()) {
    std::string text;
    if (!files::ReadFileToString(fxl::StringPrintf("/pkg/data/%s.txt", key.c_str()), &text)) {
      FX_LOGS(ERROR) << "It looks like the correct `resource` dependency has not been packaged";
      break;
    }

    auto value = std::vector<uint8_t>(text.begin(), text.end());
    store_proxy->WriteItem({key, value},
                           [&](::examples::keyvaluestore::baseline::Store_WriteItem_Result result) {
                             // Check if the FIDL call succeeded or not.
                             if (result.is_err()) {
                               FX_LOGS(INFO) << "WriteItem Error: " << result.err();
                             } else {
                               FX_LOGS(INFO) << "WriteItem Success";
                             }

                             // Quit the loop, thereby handing control back to the outer loop of
                             // actions being iterated over.
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
