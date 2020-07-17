// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/test/ui/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/syslog/global.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/zx/eventpair.h>
#include <zircon/status.h>

#include <array>
#include <memory>

namespace cpp_gfx_client {

// Implementation of a very simple Scenic client.
class CppGfxClient : public fuchsia::ui::app::ViewProvider {
 public:
  CppGfxClient(async::Loop* loop) : loop_(loop), view_provider_binding_(this) {
    FX_CHECK(loop_);

    context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          view_provider_binding_.Bind(std::move(request));
        });

    response_listener_ = context_->svc()->Connect<fuchsia::test::ui::ResponseListener>();
    response_listener_.set_error_handler([](zx_status_t status) {
      FX_LOGS(WARNING) << "JFYI. Test response listener disconnected, status: " <<
      zx_status_get_string(status);
      // Don't quit, because we should be able to run this client outside of a test.
    });

    scenic_ = context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic_.set_error_handler([this](zx_status_t status) {
      FX_LOGS(ERROR) << "Quitting. Scenic disconnected, status: " << zx_status_get_string(status);
      loop_->Quit();
    });

    session_ = std::make_unique<scenic::Session>(scenic_.get());
    session_->set_error_handler([this](zx_status_t status) {
      FX_LOGS(ERROR) << "Quitting. Scenic session disconnected, status: " <<
      zx_status_get_string(status); loop_->Quit();
    });
    session_->set_event_handler(fit::bind_member(this, &CppGfxClient::OnEvents));
    session_->set_on_frame_presented_handler([](auto frame_presented_info) {});

    root_node_ = std::make_unique<scenic::EntityNode>(session_.get());
    root_node_->SetEventMask(fuchsia::ui::gfx::kMetricsEventMask);

    session_->Present2(zx_clock_get_monotonic(), 0, [](auto future_presentation_times) {});
  }

 private:
  static constexpr std::array<std::array<uint8_t, 4>, 6> kColorsRgba = {
      {{255, 0, 0, 255},      // red
       {255, 128, 0, 255},    // orange
       {255, 255, 0, 255},    // yellow
       {0, 255, 0, 255},      // green
       {0, 0, 255, 255},      // blue
       {128, 0, 255, 255}}};  // purple

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*unused*/,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*unused*/) override {
    FX_LOGS(INFO) << "CreateView called.";

    view_ = std::make_unique<scenic::View>(session_.get(), std::move(token), "cpp-gfx-client view");
    view_->AddChild(*root_node_);

    session_->Present2(zx_clock_get_monotonic(), 0, [](auto future_presentation_times) {});
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override {
    FX_LOGS(INFO) << "CreateViewWithViewRef called.";

    view_ = std::make_unique<scenic::View>(
        session_.get(), fuchsia::ui::views::ViewToken{.value = std::move(token)},
        std::move(view_ref_control), std::move(view_ref), "cpp-gfx-client view");
    view_->AddChild(*root_node_);

    session_->Present2(zx_clock_get_monotonic(), 0, [](auto future_presentation_times) {});
  }

  // Scenic Session event handler, passed to |fuchsia::ui::scenic::SessionListener|.
  void OnEvents(std::vector<fuchsia::ui::scenic::Event> events) {
    for (const auto& event : events) {
      if (event.is_gfx()) {
        switch (event.gfx().Which()) {
          case fuchsia::ui::gfx::Event::Tag::kMetrics: {
            FX_LOGS(INFO) << "Metrics received.";
            metrics_ = event.gfx().metrics().metrics;
            break;
          }
          case fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged: {
            FX_LOGS(INFO) << "View properties received.";
            view_properties_ = event.gfx().view_properties_changed().properties;
            break;
          }
          default:
            break;  // nop
        }
      } else if (event.is_input()) {
        switch (event.input().Which()) {
          case fuchsia::ui::input::InputEvent::Tag::kPointer: {
            if (event.input().pointer().phase == fuchsia::ui::input::PointerEventPhase::DOWN &&
                material_) {
              {
                // Cycle to next color.
                color_index_ = (color_index_ + 1) % kColorsRgba.size();
                std::array<uint8_t, 4> color = kColorsRgba[color_index_];
                material_->SetColor(color[0], color[1], color[2], color[3]);
                session_->Present2(zx_clock_get_monotonic(), 0,
                                   [](auto future_presentation_times) {});
              }

              if (response_listener_) {
                fuchsia::test::ui::PointerData data;
                // The raw pointer event's coordinates are in pips (logical pixels). The test
                // expects coordinates in physical pixels. The former is transformed into the latter
                // with the scale factor provided in the metrics event.
                data.set_local_x(event.input().pointer().x * metrics_.scale_x);
                data.set_local_y(event.input().pointer().y * metrics_.scale_y);
                data.set_time_received(zx_clock_get_monotonic());
                response_listener_->Respond(std::move(data));
              }
            }
            break;
          }
          default:
            break;  // nop
        }
      }
    }

    if (!scene_created_ && ViewSize().x > 0 && ViewSize().y > 0) {
      CreateScene();
      scene_created_ = true;
    }
  }

  // Calculates view size based on view properties and metrics event.
  fuchsia::ui::gfx::vec2 ViewSize() const {
    const fuchsia::ui::gfx::ViewProperties& p = view_properties_;
    float size_x = ((p.bounding_box.max.x - p.inset_from_max.x) -
                    (p.bounding_box.min.x + p.inset_from_min.x)) *
                   metrics_.scale_x;
    float size_y = ((p.bounding_box.max.y - p.inset_from_max.y) -
                    (p.bounding_box.min.y + p.inset_from_min.y)) *
                   metrics_.scale_y;
    return fuchsia::ui::gfx::vec2{.x = size_x, .y = size_y};
  }

  // Encapsulates scene setup.
  void CreateScene() {
    FX_CHECK(session_) << "precondition";
    FX_CHECK(root_node_) << "precondition";

    scenic::Session* session = session_.get();

    scenic::ShapeNode shape(session);
    scenic::Rectangle rec(session, ViewSize().x, ViewSize().y);
    shape.SetShape(rec);
    shape.SetTranslation(ViewSize().x / 2, ViewSize().y / 2, 0.f);
    material_ = std::make_unique<scenic::Material>(session);
    std::array<uint8_t, 4> color = kColorsRgba[color_index_];
    material_->SetColor(color[0], color[1], color[2], color[3]);
    shape.SetMaterial(*material_);
    root_node_->AddChild(shape);

    session_->Present2(zx_clock_get_monotonic(), 0, [](auto future_presentation_times) {});
  }

  // The main thread's message loop.
  async::Loop* loop_ = nullptr;

  // This component's global context.
  std::unique_ptr<sys::ComponentContext> context_;

  // Protocols used by this component.
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::test::ui::ResponseListenerPtr response_listener_;

  // Protocols vended by this component.
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;

  // Scene state.
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::View> view_;
  std::unique_ptr<scenic::EntityNode> root_node_;
  std::unique_ptr<scenic::Material> material_;
  fuchsia::ui::gfx::ViewProperties view_properties_;
  fuchsia::ui::gfx::Metrics metrics_;
  bool scene_created_ = false;
  uint32_t color_index_ = 0;
};
}  // namespace cpp_gfx_client

// Component entry point.
int main(int argc, char** argv) {
  FX_LOGS(INFO) << "Starting cpp-gfx-client.";

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  cpp_gfx_client::CppGfxClient client(&loop);

  return loop.Run();
}
