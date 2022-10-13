// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <examples/keyvaluestore/baseline/cpp/fidl.h>
#include <re2/re2.h>

// An implementation of the |Store| protocol.
class StoreImpl final : public examples::keyvaluestore::baseline::Store {
 public:
  // Bind this implementation to an |InterfaceRequest|.
  StoreImpl(async_dispatcher_t* dispatcher,
            fidl::InterfaceRequest<examples::keyvaluestore::baseline::Store> request)
      : binding_(fidl::Binding<examples::keyvaluestore::baseline::Store>(this)) {
    binding_.Bind(std::move(request), dispatcher);

    // Gracefully handle abrupt shutdowns.
    binding_.set_error_handler([this](zx_status_t status) mutable {
      if (status != ZX_ERR_PEER_CLOSED) {
        FX_LOGS(ERROR) << "Shutdown unexpectedly";
      }
      delete this;
    });
  }

  // Handler for the |WriteItem| method call.
  void WriteItem(::examples::keyvaluestore::baseline::Item attempt,
                 WriteItemCallback callback) override {
    FX_LOGS(INFO) << "WriteItem request received";
    std::string key = attempt.key.data();
    const std::vector<uint8_t>& value = attempt.value;
    ::examples::keyvaluestore::baseline::Store_WriteItem_Result result;

    if (!RE2::FullMatch(key, "^[A-Za-z]\\w+[A-Za-z0-9]$")) {
      // Validate the key.
      FX_LOGS(INFO) << "Write error: INVALID_KEY, For key: " << key;
      result.set_err(examples::keyvaluestore::baseline::WriteError::INVALID_KEY);
    } else if (value.empty()) {
      // Validate the value.
      FX_LOGS(INFO) << "Write error: INVALID_VALUE, For key: " << key;
      result.set_err(examples::keyvaluestore::baseline::WriteError::INVALID_VALUE);
    } else if (key_value_store_.find(key) != key_value_store_.end()) {
      // Ensure that the value does not already exist in the store.
      FX_LOGS(INFO) << "Write error: ALREADY_EXISTS, For key: " << key;
      result.set_err(examples::keyvaluestore::baseline::WriteError::ALREADY_EXISTS);
    }

    // Check if any of the validations failed - if not, add to the store and report success.
    if (result.is_err()) {
      FX_LOGS(INFO) << "WriteItem response sent";
    } else {
      key_value_store_.insert({key, value});
      FX_LOGS(INFO) << "Wrote value at key: " << key;
      result.set_response(examples::keyvaluestore::baseline::Store_WriteItem_Response());
      FX_LOGS(INFO) << "WriteItem response sent";
    }
    callback(std::move(result));
  }

 private:
  fidl::Binding<examples::keyvaluestore::baseline::Store> binding_;

  // The map that serves as the per-connection instance of the key-value store.
  std::unordered_map<std::string, const std::vector<uint8_t>> key_value_store_ = {};
};

int main(int argc, char** argv) {
  FX_LOGS(INFO) << "Started";

  // The event loop is used to asynchronously listen for incoming connections and requests from the
  // client. The following initializes the loop, and obtains the dispatcher, which will be used when
  // binding the server implementation to a channel.
  //
  // Note that unlike the new C++ bindings, HLCPP bindings rely on the async loop being attached to
  // the current thread via the |kAsyncLoopConfigAttachToCurrentThread| configuration.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an |OutgoingDirectory| instance.
  //
  // The |component::OutgoingDirectory| class serves the outgoing directory for our component.
  // This directory is where the outgoing FIDL protocols are installed so that they can be
  // provided to other components.
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Register a handler for components trying to connect to |Store|.
  context->outgoing()->AddPublicService(
      fidl::InterfaceRequestHandler<examples::keyvaluestore::baseline::Store>(
          [dispatcher](fidl::InterfaceRequest<examples::keyvaluestore::baseline::Store> request) {
            // Create an instance of our |StoreImpl| that destroys itself when the connection
            // closes.
            new StoreImpl(dispatcher, std::move(request));
          }));

  // Everything is wired up. Sit back and run the loop until an incoming connection wakes us up.
  FX_LOGS(INFO) << "Listening for incoming connections";
  loop.Run();
  return 0;
}
