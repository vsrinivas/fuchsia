// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples.canvas.baseline/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <charconv>

#include <examples/fidl/new/canvas/baseline/cpp_wire/client/config.h>

// The |EventHandler| is a derived class that we pass into the |fidl::WireClient| to handle incoming
// events asynchronously.
class EventHandler : public fidl::WireAsyncEventHandler<examples_canvas_baseline::Instance> {
 public:
  // Handler for |OnDrawn| events sent from the server.
  void OnDrawn(fidl::WireEvent<examples_canvas_baseline::Instance::OnDrawn>* event) override {
    auto top_left = event->top_left;
    auto bottom_right = event->bottom_right;
    FX_LOGS(INFO) << "OnDrawn event received: top_left: Point { x: " << top_left.x
                  << ", y: " << top_left.y << " }, bottom_right: Point { x: " << bottom_right.x
                  << ", y: " << bottom_right.y << " }";
    loop_.Quit();
  }

  void on_fidl_error(fidl::UnbindInfo error) override { FX_LOGS(ERROR) << error; }

  explicit EventHandler(async::Loop& loop) : loop_(loop) {}

 private:
  async::Loop& loop_;
};

// A helper function that takes a coordinate in string form, like "123,-456", and parses it into a
// a struct of the form |{ in64 x; int64 y; }|.
::examples_canvas_baseline::wire::Point ParsePoint(std::string_view input) {
  int64_t x = 0;
  int64_t y = 0;
  size_t index = input.find(',');
  if (index != std::string::npos) {
    std::from_chars(input.data(), input.data() + index, x);
    std::from_chars(input.data() + index + 1, input.data() + input.length(), y);
  }
  return ::examples_canvas_baseline::wire::Point{.x = x, .y = y};
}

// A helper function that takes a coordinate pair in string form, like "1,2:-3,-4", and parses it
// into an array of 2 |Point| structs.
::fidl::Array<::examples_canvas_baseline::wire::Point, 2> ParseLine(const std::string& action) {
  auto input = std::string_view(action);
  size_t index = input.find(':');
  if (index != std::string::npos) {
    return {ParsePoint(input.substr(0, index)), ParsePoint(input.substr(index + 1))};
  }
  return {};
}

int main(int argc, const char** argv) {
  FX_LOGS(INFO) << "Started";

  // Retrieve component configuration.
  auto conf = config::Config::TakeFromStartupHandle();

  // Start up an async loop and dispatcher.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // [START connect-protocol]
  // Connect to the protocol inside the component's namespace. This can fail so it's wrapped in a
  // |zx::status| and it must be checked for errors.
  zx::status client_end = component::Connect<examples_canvas_baseline::Instance>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Instance| protocol: "
                   << client_end.status_string();
    return -1;
  }

  // Create an instance of the event handler.
  EventHandler event_handler(loop);

  // Create an asynchronous client using the newly-established connection.
  fidl::WireClient client(std::move(*client_end), dispatcher, &event_handler);
  FX_LOGS(INFO) << "Outgoing connection enabled";
  // [END connect-protocol]

  for (const auto& action : conf.script()) {
    // If the next action in the script is to "WAIT", block until an |OnDrawn| event is received
    // from the server.
    if (action == "WAIT") {
      loop.Run();
      loop.ResetQuit();
      continue;
    }

    // Draw a line to the canvas by calling the server, using the two points we just parsed
    // above as arguments.
    auto line = ParseLine(action);
    fidl::Status status = client->AddLine(line);
    if (!status.ok()) {
      // Check that our one-way call was enqueued successfully, and handle the error appropriately.
      // In the case of this example, there is nothing we can do to recover here, except to log an
      // error and exit the program.
      FX_LOGS(ERROR) << "Could not send AddLine request: " << status.status_string();
      return -1;
    }

    FX_LOGS(INFO) << "AddLine request sent: [Point { x: " << line[1].x << ", y: " << line[1].y
                  << " }, Point { x: " << line[0].x << ", y: " << line[0].y << " }]";
  }

  // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
  // referenced bug has been resolved, we can remove the sleep.
  sleep(2);
  return 0;
}
