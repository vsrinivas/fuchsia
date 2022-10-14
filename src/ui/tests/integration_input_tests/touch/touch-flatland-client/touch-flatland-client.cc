// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/test/input/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <array>
#include <memory>

#include "src/lib/ui/flatland-frame-scheduling/src/simple_present.h"

namespace touch_flatland_client {

// Implementation of a simple scenic client using the Flatland API.
class TouchFlatlandClient : public fuchsia::ui::app::ViewProvider {
 public:
  TouchFlatlandClient(async::Loop* loop) : loop_(loop), view_provider_binding_(this) {
    FX_CHECK(loop_);

    context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          view_provider_binding_.Bind(std::move(request));
        });

    touch_input_listener_ =
        context_->svc()->Connect<fuchsia::ui::test::input::TouchInputListener>();
    touch_input_listener_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "Test response listener disconnected, status: "
                       << zx_status_get_string(status);
      // Don't quit, because we should be able to run this client outside of a test.
    });

    flatland_connection_ =
        simple_present::FlatlandConnection::Create(context_.get(), "TouchFlatlandClient");
    flatland_ = flatland_connection_->flatland();

    // Set up the touch source to listen to the pointer events.
    touch_source_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "Touch source closed with status: " << zx_status_get_string(status);
    });

    // Set up parent watcher to retrieve layout info.
    parent_viewport_watcher_.set_error_handler([](zx_status_t status) {
      FX_LOGS(ERROR) << "Error from fuchsia::ui::composition::ParentViewportWatcher: "
                     << zx_status_get_string(status);
    });
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override {
    auto& view_creation_token = *args.mutable_view_creation_token();
    auto identity = scenic::NewViewIdentityOnCreation();
    fuchsia::ui::composition::ViewBoundProtocols protocols;
    protocols.set_touch_source(touch_source_.NewRequest());
    flatland_->CreateView2(std::move(view_creation_token), std::move(identity),
                           std::move(protocols), parent_viewport_watcher_.NewRequest());

    // Create a minimal scene after receiving the layout information.
    parent_viewport_watcher_->GetLayout([this](auto layout_info) {
      FX_CHECK(layout_info.has_logical_size());
      FX_CHECK(layout_info.has_device_pixel_ratio());

      width_ = layout_info.logical_size().width;
      height_ = layout_info.logical_size().width;
      display_pixel_ratio_ = layout_info.device_pixel_ratio();
      CreateScene();
    });

    // Listen for pointer events.
    touch_source_->Watch({}, fit::bind_member(this, &TouchFlatlandClient::Watch));
  }

 private:
  static constexpr std::array<std::array<float, 4>, 6> kColorsRgba = {
      {{255.f, 0.f, 0.f, 255.f},      // red
       {255.f, 128.f, 0.f, 255.f},    // orange
       {255.f, 255.f, 0.f, 255.f},    // yellow
       {0.f, 255.f, 0.f, 255.f},      // green
       {0.f, 0.f, 255.f, 255.f},      // blue
       {128.f, 0.f, 255.f, 255.f}}};  // purple

  static constexpr fuchsia::ui::composition::TransformId kRootTransformId = {1};
  static constexpr fuchsia::ui::composition::ContentId kRectId = {1};
  static constexpr fuchsia::ui::composition::TransformId kRectTransformId = {2};

  // Creates a minimal scene containing a solid filled rectangle of size |width_| * |height_|.
  // Called after receiving layout info from
  // |fuchsia.ui.composition.ParentViewportWatcher.GetLayout|.
  void CreateScene() {
    // Create the root transform
    flatland_->CreateTransform(kRootTransformId);
    flatland_->SetRootTransform(kRootTransformId);

    // Create the transform for the rectangle.
    flatland_->CreateTransform(kRectTransformId);
    flatland_->SetTranslation(kRectTransformId, {0, 0});

    // Connect the transform to the scene graph.
    flatland_->AddChild(kRootTransformId, kRectTransformId);

    // Create the content and attach it to the transform.
    auto color = kColorsRgba[color_index_];
    flatland_->CreateFilledRect(kRectId);
    flatland_->SetSolidFill(
        kRectId, {color[0] / 255.f, color[1] / 255.f, color[2] / 255.f, color[3] / 255.f},
        {width_, height_});
    flatland_->SetContent(kRectTransformId, kRectId);
    Present();
  }

  void Present() {
    flatland_connection_->Present({}, [](auto) {});
  }

  // Creates a watch loop to continuously watch for pointer events using the
  // |fuchsia.ui.pointer.TouchSource.Watch|. Changes the color of the rectangle in the scene when a
  // tap event is received.
  void Watch(std::vector<fuchsia::ui::pointer::TouchEvent> events) {
    // Stores the response for touch events in |events|.
    std::vector<fuchsia::ui::pointer::TouchResponse> responses;
    for (const auto& event : events) {
      FX_CHECK(HasValidatedTouchSample(event)) << "precondition";
      const auto& pointer_sample = event.pointer_sample();

      // Store the view parameters received from a TouchEvent when either a new connection was
      // formed or the view parameters were modified.
      if (event.has_view_parameters()) {
        view_params_ = std::move(event.view_parameters());
      }

      if (event.has_interaction_result() &&
          event.interaction_result().status ==
              fuchsia::ui::pointer::TouchInteractionStatus::GRANTED) {
        interaction_granted_ = true;
      }

      // Respond to the touch event only if the interaction has been granted.
      if (interaction_granted_) {
        if (pointer_sample.phase() == fuchsia::ui::pointer::EventPhase::ADD) {
          // Change the color of the rectangle on a tap event.
          color_index_ = (color_index_ + 1) % kColorsRgba.size();
          auto color = kColorsRgba[color_index_];
          flatland_->SetSolidFill(
              kRectId, {color[0] / 255.f, color[1] / 255.f, color[2] / 255.f, color[3] / 255.f},
              {width_, height_});
          Present();
        }
        if (touch_input_listener_) {
          fuchsia::ui::test::input::TouchInputListenerReportTouchInputRequest request;
          // Only report ADD and CHANGE events, for consistency with the
          // flutter client.
          if (pointer_sample.phase() == fuchsia::ui::pointer::EventPhase::ADD ||
              pointer_sample.phase() == fuchsia::ui::pointer::EventPhase::CHANGE) {
            auto logical = ViewportToViewCoordinates(pointer_sample.position_in_viewport(),
                                                     view_params_->viewport_to_view_transform);

            // The raw pointer event's coordinates are in pips (logical pixels). The test
            // expects coordinates in physical pixels. The former is transformed into the latter
            // with the DPR values received from |GetLayout|.
            request.set_local_x(logical[0] * display_pixel_ratio_.x)
                .set_local_y(logical[1] * display_pixel_ratio_.y)
                .set_time_received(zx_clock_get_monotonic())
                .set_component_name("touch-flatland-client");
            touch_input_listener_->ReportTouchInput(std::move(request));
          }
        }
      }

      // Reset |interaction_granted_| as the current interaction has ended.
      if (pointer_sample.phase() == fuchsia::ui::pointer::EventPhase::REMOVE) {
        interaction_granted_ = false;
      }

      fuchsia::ui::pointer::TouchResponse response;
      response.set_response_type(fuchsia::ui::pointer::TouchResponseType::YES);
      responses.push_back(std::move(response));
    }

    touch_source_->Watch(std::move(responses), fit::bind_member(this, &TouchFlatlandClient::Watch));
  }

  bool HasValidatedTouchSample(const fuchsia::ui::pointer::TouchEvent& event) {
    if (!event.has_pointer_sample()) {
      return false;
    }
    FX_CHECK(event.pointer_sample().has_interaction()) << "API guarantee";
    FX_CHECK(event.pointer_sample().has_phase()) << "API guarantee";
    FX_CHECK(event.pointer_sample().has_position_in_viewport()) << "API guarantee";
    return true;
  }

  std::array<float, 2> ViewportToViewCoordinates(
      std::array<float, 2> viewport_coordinates,
      const std::array<float, 9>& viewport_to_view_transform) {
    // The transform matrix is a FIDL array with matrix data in column-major
    // order. For a matrix with data [a b c d e f g h i], and with the viewport
    // coordinates expressed as homogeneous coordinates, the logical view
    // coordinates are obtained with the following formula:
    //   |a d g|   |x|   |x'|
    //   |b e h| * |y| = |y'|
    //   |c f i|   |1|   |w'|
    // which we then normalize based on the w component:
    //   if w' not zero: (x'/w', y'/w')
    //   else (x', y')
    const auto& M = viewport_to_view_transform;
    const float x = viewport_coordinates[0];
    const float y = viewport_coordinates[1];
    const float xp = M[0] * x + M[3] * y + M[6];
    const float yp = M[1] * x + M[4] * y + M[7];
    const float wp = M[2] * x + M[5] * y + M[8];
    if (wp != 0) {
      return {xp / wp, yp / wp};
    } else {
      return {xp, yp};
    }
  }

  // The main thread's message loop.
  async::Loop* loop_ = nullptr;

  // This component's global context.
  std::unique_ptr<sys::ComponentContext> context_;

  // Protocols used by this component.
  fuchsia::ui::test::input::TouchInputListenerPtr touch_input_listener_;

  // Protocols vended by this component.
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;

  fuchsia::ui::composition::Flatland* flatland_;

  std::unique_ptr<simple_present::FlatlandConnection> flatland_connection_;

  fuchsia::ui::pointer::TouchSourcePtr touch_source_;

  fuchsia::ui::composition::ParentViewportWatcherPtr parent_viewport_watcher_;

  // The fuchsia.ui.pointer.TouchSource protocol issues channel-global view
  // parameters on connection and on change. Events must apply these view
  // parameters to correctly map to logical view coordinates. The "nullopt"
  // state represents the absence of view parameters, early in the protocol
  // lifecycle.
  std::optional<fuchsia::ui::pointer::ViewParameters> view_params_;

  uint32_t color_index_ = 0;

  // Logical width and height of the view received from
  // |fuchsia.ui.composition.ParentViewportWatcher.GetLayout|.
  uint32_t width_ = 0;
  uint32_t height_ = 0;

  // DPR received from |fuchsia.ui.composition.ParentViewportWatcher.GetLayout|.
  fuchsia::math::VecF display_pixel_ratio_ = {.x = 1.f, .y = 1.f};

  // Indicates whether the latest touch interaction has been granted to the client.
  bool interaction_granted_ = false;
};
}  // namespace touch_flatland_client

int main(int argc, char** argv) {
  FX_LOGS(INFO) << "Starting Flatland cpp client";
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  touch_flatland_client::TouchFlatlandClient client(&loop);

  return loop.Run();
}
