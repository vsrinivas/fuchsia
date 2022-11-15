// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples.keyvaluestore.supportexports/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

// [START diff_1]
#include <algorithm>
// [END diff_1]

#include <re2/re2.h>

// An implementation of the |Store| protocol.
class StoreImpl final : public fidl::Server<examples_keyvaluestore_supportexports::Store> {
 public:
  // Bind this implementation to a channel.
  StoreImpl(async_dispatcher_t* dispatcher,
            fidl::ServerEnd<examples_keyvaluestore_supportexports::Store> server_end)
      : binding_(fidl::BindServer(
            dispatcher, std::move(server_end), this,
            [this](StoreImpl* impl, fidl::UnbindInfo info,
                   fidl::ServerEnd<examples_keyvaluestore_supportexports::Store> server_end) {
              if (info.reason() != ::fidl::Reason::kPeerClosed) {
                FX_LOGS(ERROR) << "Shutdown unexpectedly";
              }
              delete this;
            })) {}

  void WriteItem(WriteItemRequest& request, WriteItemCompleter::Sync& completer) override {
    FX_LOGS(INFO) << "WriteItem request received";
    auto key = request.attempt().key();
    auto value = request.attempt().value();

    // Validate the key.
    if (!RE2::FullMatch(key, "^[A-Za-z]\\w+[A-Za-z0-9]$")) {
      FX_LOGS(INFO) << "Write error: INVALID_KEY, For key: " << key;
      FX_LOGS(INFO) << "WriteItem response sent";
      return completer.Reply(
          fit::error(examples_keyvaluestore_supportexports::WriteError::kInvalidKey));
    }

    // Validate the value.
    if (value.empty()) {
      FX_LOGS(INFO) << "Write error: INVALID_VALUE, For key: " << key;
      FX_LOGS(INFO) << "WriteItem response sent";
      return completer.Reply(
          fit::error(examples_keyvaluestore_supportexports::WriteError::kInvalidValue));
    }

    if (key_value_store_.find(key) != key_value_store_.end()) {
      FX_LOGS(INFO) << "Write error: ALREADY_EXISTS, For key: " << key;
      FX_LOGS(INFO) << "WriteItem response sent";
      return completer.Reply(
          fit::error(examples_keyvaluestore_supportexports::WriteError::kAlreadyExists));
    }

    // Ensure that the value does not already exist in the store.
    key_value_store_.insert({key, value});
    FX_LOGS(INFO) << "Wrote value at key: " << key;
    FX_LOGS(INFO) << "WriteItem response sent";
    return completer.Reply(fit::ok());
  }

  // [START diff_2]
  void Export(ExportRequest& request, ExportCompleter::Sync& completer) override {
    FX_LOGS(INFO) << "Export request received";
    completer.Reply(Export(std::move(request.empty())));
    FX_LOGS(INFO) << "Export response sent";
  }
  // [END diff_2]

 private:
  // [START diff_3]
  using ExportError = ::examples_keyvaluestore_supportexports::ExportError;
  using Exportable = ::examples_keyvaluestore_supportexports::Exportable;
  using Item = ::examples_keyvaluestore_supportexports::Item;

  fit::result<ExportError, zx::vmo> Export(zx::vmo vmo) {
    if (key_value_store_.empty()) {
      return fit::error(ExportError::kEmpty);
    }
    Exportable exportable;
    std::vector<Item> items;
    items.reserve(key_value_store_.size());
    for (const auto& [k, v] : key_value_store_) {
      items.push_back(Item{{.key = k, .value = v}});
    }
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.key() < b.key(); });
    exportable.items(std::move(items));
    fit::result encoded = fidl::Persist(exportable);
    if (encoded.is_error()) {
      FX_LOGS(ERROR) << "Failed to encode in persistence convention: " << encoded.error_value();
      return fit::error(ExportError::kUnknown);
    }
    size_t content_size = 0;
    if (vmo.get_prop_content_size(&content_size) != ZX_OK) {
      return fit::error(ExportError::kUnknown);
    }
    if (encoded->size() > content_size) {
      return fit::error(ExportError::kStorageTooSmall);
    }
    if (vmo.set_prop_content_size(encoded->size()) != ZX_OK) {
      return fit::error(ExportError::kUnknown);
    }
    if (vmo.write(encoded->data(), 0, encoded->size()) != ZX_OK) {
      return fit::error(ExportError::kUnknown);
    }
    return fit::ok(std::move(vmo));
  }
  // [END diff_3]

  fidl::ServerBindingRef<examples_keyvaluestore_supportexports::Store> binding_;

  // The map that serves as the per-connection instance of the key-value store.
  std::unordered_map<std::string, std::vector<uint8_t>> key_value_store_ = {};
};

int main(int argc, char** argv) {
  FX_LOGS(INFO) << "Started";

  // The event loop is used to asynchronously listen for incoming connections and requests from the
  // client. The following initializes the loop, and obtains the dispatcher, which will be used when
  // binding the server implementation to a channel.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an |OutgoingDirectory| instance.
  //
  // The |component::OutgoingDirectory| class serves the outgoing directory for our component. This
  // directory is where the outgoing FIDL protocols are installed so that they can be provided to
  // other components.
  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);

  // The `ServeFromStartupInfo()` function sets up the outgoing directory with the startup handle.
  // The startup handle is a handle provided to every component by the system, so that they can
  // serve capabilities (e.g. FIDL protocols) to other components.
  zx::result result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to serve outgoing directory: " << result.status_string();
    return -1;
  }

  // Register a handler for components trying to connect to |Store|.
  result = outgoing.AddProtocol<examples_keyvaluestore_supportexports::Store>(
      [dispatcher](fidl::ServerEnd<examples_keyvaluestore_supportexports::Store> server_end) {
        // Create an instance of our StoreImpl that destroys itself when the connection closes.
        new StoreImpl(dispatcher, std::move(server_end));
      });
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to add Store protocol: " << result.status_string();
    return -1;
  }

  // Everything is wired up. Sit back and run the loop until an incoming connection wakes us up.
  FX_LOGS(INFO) << "Listening for incoming connections";
  loop.Run();
  return 0;
}
