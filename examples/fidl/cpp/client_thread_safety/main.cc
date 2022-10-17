// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/sys/component/cpp/service_client.h>
#include <zircon/assert.h>

#include <future>
#include <iostream>

using fuchsia_examples::Echo;

// [START owned_event_handler]
void OwnedEventHandler(async_dispatcher_t* dispatcher, fidl::ClientEnd<Echo> client_end) {
  // Define some blocking futures to maintain a consistent sequence of events
  // for the purpose of this example. Production code usually won't need these.
  std::promise<void> teardown;
  std::future<void> teardown_complete = teardown.get_future();
  std::promise<void> reply;
  std::future<void> got_reply = reply.get_future();

  // Define the event handler for the client. The event handler is always
  // placed in a |std::unique_ptr| in the owned event handler pattern.
  // When the |EventHandler| is destroyed, we know that binding teardown
  // has completed.
  class EventHandler : public fidl::AsyncEventHandler<Echo> {
   public:
    explicit EventHandler(std::promise<void>& teardown, std::promise<void>& reply)
        : teardown_(teardown), reply_(reply) {}

    void on_fidl_error(fidl::UnbindInfo error) override {
      // This handler is invoked by the bindings when an error causes it to
      // teardown prematurely. Note that additionally cleanup is typically
      // performed in the destructor of the event handler, since both manually
      // initiated teardown and error teardown will destroy the event handler.
      std::cerr << "Error in Echo client: " << error;

      // In this example, we abort the process when an error happens. Production
      // code should handle the error gracefully (by cleanly exiting or attempt
      // to recover).
      abort();
    }

    ~EventHandler() override {
      // Additional cleanup may be performed here.

      // Notify the outer function.
      teardown_.set_value();
    }

    // Regular event handling code is also supported.
    void OnString(fidl::Event<Echo::OnString>& event) override {
      std::string response(event.response().data(), event.response().size());
      std::cout << "Got event: " << response << std::endl;
    }

    void OnEchoStringResponse(fuchsia_examples::EchoEchoStringResponse& response) {
      std::string reply(response.response().data(), response.response().size());
      std::cout << "Got response: " << reply << std::endl;

      if (!notified_reply_) {
        reply_.set_value();
        notified_reply_ = true;
      }
    }

   private:
    std::promise<void>& teardown_;
    std::promise<void>& reply_;
    bool notified_reply_ = false;
  };
  std::unique_ptr handler = std::make_unique<EventHandler>(teardown, reply);
  EventHandler* handler_ptr = handler.get();

  // Create a client that owns the event handler.
  fidl::SharedClient client(std::move(client_end), dispatcher, std::move(handler));

  // Make an EchoString call, passing it a callback that captures the event
  // handler.
  client->EchoString({"hello"}).ThenExactlyOnce(
      [handler_ptr](fidl::Result<Echo::EchoString>& result) {
        ZX_ASSERT(result.is_ok());
        auto& response = result.value();
        handler_ptr->OnEchoStringResponse(response);
      });
  got_reply.wait();

  // Make another call but immediately start binding teardown afterwards.
  // The reply may race with teardown; the callback is always canceled if
  // teardown finishes before a response is received.
  client->EchoString({"hello"}).ThenExactlyOnce(
      [handler_ptr](fidl::Result<Echo::EchoString>& result) {
        if (result.is_ok()) {
          auto& response = result.value();
          handler_ptr->OnEchoStringResponse(response);
        } else {
          // Teardown finished first.
          ZX_ASSERT(result.error_value().is_canceled());
        }
      });

  // Begin tearing down the client.
  // This does not have to happen on the dispatcher thread.
  client.AsyncTeardown();

  teardown_complete.wait();
}
// [END owned_event_handler]

namespace {

class MyObject : public fidl::AsyncEventHandler<Echo> {};

}  // namespace

void CustomCallback(async_dispatcher_t* dispatcher, fidl::ClientEnd<Echo> client_end) {
  // [START custom_callback]
  fidl::SharedClient<Echo> client;

  // Let's say |my_object| is constructed on the heap;
  MyObject* my_object = new MyObject;
  // ... and needs to be freed via `delete`.
  auto observer = fidl::ObserveTeardown([my_object] {
    std::cout << "client is tearing down" << std::endl;
    delete my_object;
  });

  // |my_object| may implement |fidl::AsyncEventHandler<Echo>|.
  // |observer| will be notified and destroy |my_object| after teardown.
  client.Bind(std::move(client_end), dispatcher, my_object, std::move(observer));
  // [END custom_callback]
}

void ShareUntilTeardown(async_dispatcher_t* dispatcher, fidl::ClientEnd<Echo> client_end) {
  // [START share_until_teardown]
  fidl::SharedClient<Echo> client;

  // Let's say |my_object| is always managed by a shared pointer.
  std::shared_ptr<MyObject> my_object = std::make_shared<MyObject>();

  // |my_object| will be kept alive as long as the binding continues
  // to exist. When teardown completes, |my_object| will be destroyed
  // only if there are no other shared references (such as from other
  // related user objects).
  auto observer = fidl::ShareUntilTeardown(my_object);
  client.Bind(std::move(client_end), dispatcher, my_object.get(), std::move(observer));
  // [END share_until_teardown]
}

fidl::ClientEnd<Echo> ConnectToEcho() {
  zx::result svc = component::OpenServiceRoot();
  ZX_ASSERT_MSG(svc.is_ok(), "Failed to open service root: %s", svc.status_string());
  zx::result client_end = component::ConnectAt<Echo>(*svc);
  ZX_ASSERT_MSG(client_end.is_ok(), "Failed to connect to Echo protocol: %s",
                client_end.status_string());
  return std::move(client_end.value());
}

int main(int argc, const char** argv) {
  // Refer to the async client tutorial for explanation about async loops and
  // connecting to services:
  // https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/client
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  loop.StartThread("thread-1");
  loop.StartThread("thread-2");

  OwnedEventHandler(dispatcher, ConnectToEcho());
  CustomCallback(dispatcher, ConnectToEcho());
  ShareUntilTeardown(dispatcher, ConnectToEcho());

  return 0;
}
