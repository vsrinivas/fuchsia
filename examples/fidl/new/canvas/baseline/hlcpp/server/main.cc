// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <unistd.h>

#include <examples/canvas/cpp/fidl.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/weak_ptr.h>

// A struct that stores the two things we care about for this example: the set of lines, and the
// bounding box that contains them.
struct CanvasState {
  // Tracks whether there has been a change since the last send, to prevent redundant updates.
  bool changed = true;
  examples::canvas::BoundingBox bounding_box;
};

// An implementation of the |Instance| protocol.
class InstanceImpl final : public examples::canvas::Instance {
 public:
  // Bind this implementation to an |InterfaceRequest|.
  InstanceImpl(async_dispatcher_t* dispatcher,
               fidl::InterfaceRequest<examples::canvas::Instance> request)
      : binding_(fidl::Binding<examples::canvas::Instance>(this)), weak_factory_(this) {
    binding_.Bind(std::move(request), dispatcher);

    // Gracefully handle abrupt shutdowns.
    binding_.set_error_handler([this](zx_status_t status) mutable {
      if (status != ZX_ERR_PEER_CLOSED) {
        FX_LOGS(ERROR) << "Shutdown unexpectedly";
      }
      delete this;
    });

    // Start the update timer on startup. Our server sends one update per second.
    ScheduleOnDrawnEvent(dispatcher, zx::sec(1));
  }

  void AddLine(::std::array<::examples::canvas::Point, 2> line) override {
    FX_LOGS(INFO) << "AddLine request received: [Point { x: " << line[1].x << ", y: " << line[1].y
                  << " }, Point { x: " << line[0].x << ", y: " << line[0].y << " }]";

    // Update the bounding box to account for the new line we've just "added" to the canvas.
    auto& bounds = state_.bounding_box;
    for (const auto& point : line) {
      if (point.x < bounds.top_left.x) {
        bounds.top_left.x = point.x;
      }
      if (point.y > bounds.top_left.y) {
        bounds.top_left.y = point.y;
      }
      if (point.x > bounds.bottom_right.x) {
        bounds.bottom_right.x = point.x;
      }
      if (point.y < bounds.bottom_right.y) {
        bounds.bottom_right.y = point.y;
      }
    }

    // Mark the state as "dirty", so that an update is sent back to the client on the next |OnDrawn|
    // event.
    state_.changed = true;
  }

 private:
  // Each scheduled update waits for the allotted amount of time, sends an update if something has
  // changed, and schedules the next update.
  void ScheduleOnDrawnEvent(async_dispatcher_t* dispatcher, zx::duration after) {
    async::PostDelayedTask(
        dispatcher,
        [&, dispatcher, after, weak = weak_factory_.GetWeakPtr()] {
          // Halt execution if the binding has been deallocated already.
          if (!weak) {
            return;
          }

          // Schedule the next update if the binding still exists.
          weak->ScheduleOnDrawnEvent(dispatcher, after);

          // No need to send an update if nothing has changed since the last one.
          if (!weak->state_.changed) {
            return;
          }

          // This is where we would draw the actual lines. Since this is just an example, we'll
          // avoid doing the actual rendering, and simply send the bounding box to the client
          // instead.
          auto top_left = state_.bounding_box.top_left;
          auto bottom_right = state_.bounding_box.bottom_right;
          binding_.events().OnDrawn(top_left, bottom_right);
          FX_LOGS(INFO) << "OnDrawn event sent: top_left: Point { x: " << top_left.x
                        << ", y: " << top_left.y
                        << " }, bottom_right: Point { x: " << bottom_right.x
                        << ", y: " << bottom_right.y << " }";

          // Reset the change tracker.
          state_.changed = false;
        },
        after);
  }

  fidl::Binding<examples::canvas::Instance> binding_;
  CanvasState state_ = CanvasState{};

  // Generates weak references to this object, which are appropriate to pass into asynchronous
  // callbacks that need to access this object. The references are automatically invalidated
  // if this object is destroyed.
  fxl::WeakPtrFactory<InstanceImpl> weak_factory_;
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

  // Register a handler for components trying to connect to |examples.canvas.Instance|.
  context->outgoing()->AddPublicService(fidl::InterfaceRequestHandler<examples::canvas::Instance>(
      [dispatcher](fidl::InterfaceRequest<examples::canvas::Instance> request) {
        // Create an instance of our |InstanceImpl| that destroys itself when the connection closes.
        new InstanceImpl(dispatcher, std::move(request));
      }));

  // Everything is wired up. Sit back and run the loop until an incoming connection wakes us up.
  FX_LOGS(INFO) << "Listening for incoming connections";
  loop.Run();
  return 0;
}
