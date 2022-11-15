// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples.keyvaluestore.supportexports/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <examples/fidl/new/key_value_store/support_exports/cpp_natural/client/config.h>
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
  zx::result client_end = component::Connect<examples_keyvaluestore_supportexports::Store>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Store| protocol: "
                   << client_end.status_string();
    return -1;
  }

  // Create an asynchronous client using the newly-established connection.
  fidl::Client client(std::move(*client_end), dispatcher);
  FX_LOGS(INFO) << "Outgoing connection enabled";

  for (const auto& action : conf.write_items()) {
    std::string text;
    if (!files::ReadFileToString(fxl::StringPrintf("/pkg/data/%s.txt", action.c_str()), &text)) {
      FX_LOGS(ERROR) << "It looks like the correct `resource` dependency has not been packaged";
      break;
    }

    auto value = std::vector<uint8_t>(text.begin(), text.end());
    client->WriteItem(examples_keyvaluestore_supportexports::Item(action, value))
        .ThenExactlyOnce(
            [&](fidl::Result<examples_keyvaluestore_supportexports::Store::WriteItem> result) {
              // Check if the FIDL call succeeded or not.
              if (!result.is_ok()) {
                if (result.error_value().is_framework_error()) {
                  FX_LOGS(ERROR) << "Unexpected FIDL framework error: " << result.error_value();
                } else {
                  FX_LOGS(INFO) << "WriteItem Error: "
                                << fidl::ToUnderlying(result.error_value().domain_error());
                }
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

  // [START diff_1]
  // If the `max_export_size` is 0, no export is possible, so just ignore this block. This check
  // isn't strictly necessary, but does avoid extra work down the line.
  if (conf.max_export_size() > 0) {
    // Create a 100Kb VMO to store the resulting export. In a real implementation, we would
    // likely receive the VMO representing the to-be-written file from file system like vfs of
    // fxfs.
    zx::vmo vmo;
    if (zx_status_t status = zx::vmo::create(conf.max_export_size(), 0, &vmo); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to create VMO";
      return -1;
    }

    client->Export({std::move(vmo)})
        .ThenExactlyOnce(
            [&](fidl::Result<examples_keyvaluestore_supportexports::Store::Export>& result) {
              // Quit the loop, thereby handing control back to the outer loop of actions being
              // iterated over, when we return from this callback.
              loop.Quit();

              if (!result.is_ok()) {
                if (result.error_value().is_framework_error()) {
                  FX_LOGS(ERROR) << "Unexpected FIDL framework error: " << result.error_value();
                } else {
                  FX_LOGS(INFO) << "Export Error: "
                                << fidl::ToUnderlying(result.error_value().domain_error());
                }
                return;
              }

              FX_LOGS(INFO) << "Export Success";
              // Read the exported data (encoded in byte form as persistent FIDL) from the
              // returned VMO. In a real implementation, instead of reading the VMO, we would
              // merely forward it to some other storage-handling process. Doing this using a VMO,
              // rather than FIDL IPC, would save us frivolous reads and writes at each hop.
              size_t content_size = 0;
              zx::vmo vmo = std::move(result->filled());
              if (vmo.get_prop_content_size(&content_size) != ZX_OK) {
                return;
              }
              std::vector<uint8_t> encoded_bytes;
              encoded_bytes.resize(content_size);
              if (vmo.read(encoded_bytes.data(), 0, content_size) != ZX_OK) {
                return;
              }
              // Decode the persistent FIDL that was just read from the file.
              fit::result exportable =
                  fidl::Unpersist<examples_keyvaluestore_supportexports::Exportable>(
                      cpp20::span(encoded_bytes));
              if (exportable.is_error()) {
                FX_LOGS(ERROR) << "Failed to unpersist: " << exportable.error_value();
                return;
              }
              if (!exportable->items().has_value()) {
                FX_LOGS(INFO) << "Expected items to be set";
                return;
              }
              auto& items = exportable->items().value();

              // Log some information about the exported data.
              FX_LOGS(INFO) << "Printing " << items.size() << " exported entries, which are:";
              for (const auto& item : items) {
                FX_LOGS(INFO) << "  * " << item.key();
              }
            });

    // Run the loop until the callback is resolved, at which point we can continue from here.
    loop.Run();
    loop.ResetQuit();
  }
  // [END diff_1]

  // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
  // referenced bug has been resolved, we can remove the sleep.
  sleep(2);
  return 0;
}
