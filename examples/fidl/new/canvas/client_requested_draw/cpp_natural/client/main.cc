// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples.canvas.clientrequesteddraw/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/incoming/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>

#include <charconv>

#include <examples/fidl/new/canvas/client_requested_draw/cpp_natural/client/config.h>

// The |EventHandler| is a derived class that we pass into the |fidl::WireClient| to handle incoming
// events asynchronously.
class EventHandler : public fidl::AsyncEventHandler<examples_canvas_clientrequesteddraw::Instance> {
 public:
  // Handler for |OnDrawn| events sent from the server.
  void OnDrawn(
      fidl::Event<examples_canvas_clientrequesteddraw::Instance::OnDrawn>& event) override {
    ::examples_canvas_clientrequesteddraw::Point top_left = event.top_left();
    ::examples_canvas_clientrequesteddraw::Point bottom_right = event.bottom_right();
    FX_LOGS(INFO) << "OnDrawn event received: top_left: Point { x: " << top_left.x()
                  << ", y: " << top_left.y() << " }, bottom_right: Point { x: " << bottom_right.x()
                  << ", y: " << bottom_right.y() << " }";
    loop_.Quit();
  }

  void on_fidl_error(fidl::UnbindInfo error) override { FX_LOGS(ERROR) << error; }

  explicit EventHandler(async::Loop& loop) : loop_(loop) {}

 private:
  async::Loop& loop_;
};

// A helper function that takes a coordinate in string form, like "123,-456", and parses it into a
// a struct of the form |{ in64 x; int64 y; }|.
::examples_canvas_clientrequesteddraw::Point ParsePoint(std::string_view input) {
  int64_t x = 0;
  int64_t y = 0;
  size_t index = input.find(',');
  if (index != std::string::npos) {
    std::from_chars(input.data(), input.data() + index, x);
    std::from_chars(input.data() + index + 1, input.data() + input.length(), y);
  }
  return ::examples_canvas_clientrequesteddraw::Point(x, y);
}

using Line = ::std::array<::examples_canvas_clientrequesteddraw::Point, 2>;

// A helper function that takes a coordinate pair in string form, like "1,2:-3,-4", and parses it
// into an array of 2 |Point| structs.
Line ParseLine(const std::string& action) {
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

  // Connect to the protocol inside the component's namespace. This can fail so it's wrapped in a
  // |zx::result| and it must be checked for errors.
  zx::result client_end = component::Connect<examples_canvas_clientrequesteddraw::Instance>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Instance| protocol: "
                   << client_end.status_string();
    return -1;
  }

  // Create an instance of the event handler.
  EventHandler event_handler(loop);

  // Create an asynchronous client using the newly-established connection.
  fidl::Client client(std::move(*client_end), dispatcher, &event_handler);
  FX_LOGS(INFO) << "Outgoing connection enabled";

  // [START diff_1]
  std::vector<Line> batched_lines;
  for (const auto& action : conf.script()) {
    // If the next action in the script is to "PUSH", send a batch of lines to the server.
    if (action == "PUSH") {
      fit::result<fidl::Error> result = client->AddLines(batched_lines);
      if (!result.is_ok()) {
        // Check that our one-way call was enqueued successfully, and handle the error
        // appropriately. In the case of this example, there is nothing we can do to recover here,
        // except to log an error and exit the program.
        FX_LOGS(ERROR) << "Could not send AddLines request: " << result.error_value();
        return -1;
      }

      batched_lines.clear();
      FX_LOGS(INFO) << "AddLines request sent";
      continue;
    }
    // [END diff_1]

    // If the next action in the script is to "WAIT", block until an |OnDrawn| event is received
    // from the server.
    if (action == "WAIT") {
      loop.Run();
      loop.ResetQuit();

      // [START diff_2]
      // Now, inform the server that we are ready to receive more updates whenever they are
      // ready for us.
      FX_LOGS(INFO) << "Ready request sent";
      client->Ready().ThenExactlyOnce(
          [&](fidl::Result<examples_canvas_clientrequesteddraw::Instance::Ready> result) {
            // Check if the FIDL call succeeded or not.
            if (result.is_ok()) {
              FX_LOGS(INFO) << "Ready success";
            } else {
              FX_LOGS(ERROR) << "Could not send Ready request: " << result.error_value();
            }

            // Quit the loop, thereby handing control back to the outer loop of actions being
            // iterated over.
            loop.Quit();
          });

      // Run the loop until the callback is resolved, at which point we can continue from here.
      loop.Run();
      loop.ResetQuit();
      // [END diff_2]

      continue;
    }

    // [START diff_3]
    // Batch a line for drawing to the canvas using the two points provided.
    Line line = ParseLine(action);
    batched_lines.push_back(line);
    FX_LOGS(INFO) << "AddLines batching line: [Point { x: " << line[1].x() << ", y: " << line[1].y()
                  << " }, Point { x: " << line[0].x() << ", y: " << line[0].y() << " }]";
    // [END diff_3]
  }

  // TODO(fxbug.dev/76579): We need to sleep here to make sure all logs get drained. Once the
  // referenced bug has been resolved, we can remove the sleep.
  sleep(2);
  return 0;
}
