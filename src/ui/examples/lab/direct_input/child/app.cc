// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/lab/direct_input/child/app.h"

#include <fuchsia/math/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <limits>
#include <string>

#include "lib/ui/scenic/cpp/resources.h"

namespace direct_input_child {

const uint32_t kNoFinger = std::numeric_limits<uint32_t>::max();  // Sentinel.

App::App(async::Loop* loop)
    : component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      message_loop_(loop),
      focused_(false),
      view_provider_binding_(this) {
  FX_DCHECK(component_context_);

  scenic_ = component_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this](zx_status_t status) { OnScenicError(); });
  FX_LOGS(INFO) << "Child - connect to Scenic.";

  session_ = std::make_unique<scenic::Session>(scenic_.get());
  session_->set_error_handler([this](zx_status_t status) { this->OnSessionError(); });
  session_->set_event_handler(fit::bind_member(this, &App::OnSessionEvents));
  FX_LOGS(INFO) << "Child - session setup.";

  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
    CreateScene(display_info.width_in_px, display_info.height_in_px);
    UpdateScene(zx_clock_get_monotonic());
  });

  component_context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        view_provider_binding_.Bind(std::move(request));
      },
      "view_provider");

  {
    fuchsia::ui::input::SetHardKeyboardDeliveryCmd cmd;
    cmd.delivery_request = true;
    fuchsia::ui::input::Command input_cmd;
    input_cmd.set_set_hard_keyboard_delivery(std::move(cmd));
    session_->Enqueue(std::move(input_cmd));
  }

  FX_LOGS(INFO) << "Child - ViewProvider service set up.";
}

App::~App() {
  component_context_->outgoing()->RemovePublicService<fuchsia::ui::app::ViewProvider>();
  ReleaseSessionResources();
}

void App::ReleaseSessionResources() {
  if (session_) {
    root_node_.reset();
    focus_frame_.reset();
    for (size_t i = 0; i < 10; ++i) {
      pointer_id_[i] = kNoFinger;
      pointer_tracker_[i].reset();
    }
    view_.reset();

    session_->Flush();
    session_.reset();
  }
}

void App::OnScenicError() {
  FX_LOGS(ERROR) << "Child - scenic connection error.";
  ReleaseSessionResources();
  message_loop_->Quit();
}

void App::OnSessionError() {
  FX_LOGS(ERROR) << "Child - session error.";
  ReleaseSessionResources();
  message_loop_->Quit();
}

void App::OnSessionClose() {
  FX_LOGS(INFO) << "Child - session close.";
  ReleaseSessionResources();
  message_loop_->Quit();
}

void App::OnSessionEvents(std::vector<fuchsia::ui::scenic::Event> events) {
  using InputEvent = fuchsia::ui::input::InputEvent;
  using InputType = fuchsia::ui::input::InputEvent::Tag;

  for (const auto& event : events) {
    if (event.is_input()) {
      const InputEvent& input_event = event.input();
      switch (input_event.Which()) {
        case InputType::kPointer:
          OnPointerEvent(input_event.pointer());
          continue;
        case InputType::kKeyboard:
          OnKeyboardEvent(input_event.keyboard());
          continue;
        case InputType::kFocus:
          OnFocusEvent(input_event.focus());
          continue;
        case InputType::Invalid:
          FX_LOGS(FATAL) << "Unknown input event received.";
      }
    } else if (event.is_gfx()) {
      FX_LOGS(ERROR) << "Child - GFX command unimplemented.";
    }
  }
}

void App::OnFocusEvent(const fuchsia::ui::input::FocusEvent& event) {
  focused_ = event.focused;
  if (focused_) {
    view_->AddChild(*focus_frame_);
  } else {
    view_->DetachChild(*focus_frame_);
  }
}

void App::OnKeyboardEvent(const fuchsia::ui::input::KeyboardEvent& event) {
  using Phase = fuchsia::ui::input::KeyboardEventPhase;
  // "Blink" the focus frame to acknowledge keyboard event.
  if (event.phase == Phase::PRESSED) {
    view_->DetachChild(*focus_frame_);
    async::PostDelayedTask(
        message_loop_->dispatcher(),
        [this]() {
          if (focused_) {
            view_->AddChild(*focus_frame_);
          }
        },
        zx::msec(80));
  }
}

// Helper function for OnPointerEvent.  Find index of matching element.
template <std::size_t N>
static size_t find_idx(const std::array<uint32_t, N>& array, uint32_t elem) {
  for (size_t i = 0; i < N; ++i) {
    if (array[i] == elem) {
      return i;
    }
  }
  return kNoFinger;  // Keep it simple.
}

// Helper function for OnPointerEvent.  Return contents of array as a string.
template <std::size_t N>
static std::string contents(const std::array<uint32_t, N>& array) {
  std::string value = "[";
  for (size_t i = 0; i < N; ++i) {
    value += std::to_string(array[i]);
    if (i < N - 1) {
      value += ", ";
    }
  }
  return value + "]";
}

// This function implements a very specific input-recognition behavior.
// Despite parallel dispatch of input events on a DOWN hit, we only track a
// pointer if we are also focused. In contrast, gestures need to see all inputs,
// regardless of focus state.
void App::OnPointerEvent(const fuchsia::ui::input::PointerEvent& event) {
  using Type = fuchsia::ui::input::PointerEventType;
  using Phase = fuchsia::ui::input::PointerEventPhase;

  if (event.type == Type::TOUCH) {
    // TODO(fxbug.dev/24137): Reduce the very noticeable tracking lag.
    if (focused_ && event.phase == Phase::DOWN) {
      // Nice to meet you. Add to known-fingers list.
      size_t idx = find_idx(pointer_id_, kNoFinger);
      FX_CHECK(idx != kNoFinger) << "Pointer index full: " << contents(pointer_id_);
      pointer_id_[idx] = event.pointer_id;
      view_->AddChild(*pointer_tracker_[idx]);
      pointer_tracker_[idx]->SetTranslation(event.x, event.y, -400.f);

    } else if (event.phase == Phase::MOVE) {
      size_t idx = find_idx(pointer_id_, event.pointer_id);
      if (idx != kNoFinger) {
        // It's a finger we know, keep moving.
        pointer_tracker_[idx]->SetTranslation(event.x, event.y, -400.f);
      }

    } else if (event.phase == Phase::UP || event.phase == Phase::CANCEL) {
      size_t idx = find_idx(pointer_id_, event.pointer_id);
      if (idx != kNoFinger) {
        // It's a finger we know, but time to remove.
        view_->DetachChild(*pointer_tracker_[idx]);
        pointer_id_[idx] = kNoFinger;
      }
    }
  }
}

void App::CreateView(zx::eventpair view_token,
                     fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                     fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  FX_LOGS(INFO) << "Child - CreateView invoked.";
  view_ = std::make_unique<scenic::View>(session_.get(), std::move(view_token), "child view");
  view_->SetLabel("child view");

  if (root_node_) {
    view_->AddChild(*root_node_);
    FX_LOGS(INFO) << "Child - outbound view is set up.";
  }
}

void App::UpdateScene(uint64_t next_presentation_time) {
  session_->Present(next_presentation_time, [this](fuchsia::images::PresentationInfo info) {
    UpdateScene(info.presentation_time + 2 * info.presentation_interval);
  });
}

void App::CreateScene(float display_width, float display_height) {
  FX_LOGS(INFO) << "Child - display size: " << display_width << ", " << display_height;

  width_in_px_ = display_width;  // Store display size, not view size!
  height_in_px_ = display_height;

  auto session = session_.get();

  const float kMargin = 100.f;
  const float kWidth = display_width - 6.f * kMargin;
  const float kHeight = display_height - 6.f * kMargin;

  // Set up root node, expose it to outbound View.
  {
    std::unique_ptr<scenic::EntityNode> root_node = std::make_unique<scenic::EntityNode>(session);
    root_node->SetLabel("child root node");
    root_node->SetTranslation(0.f, 0.f, -100.f);

    scenic::ShapeNode shape(session);
    scenic::Rectangle rec(session, kWidth, kHeight);
    shape.SetShape(rec);
    scenic::Material material(session);
    material.SetColor(0, 191, 255, 255);  // Light blue
    shape.SetMaterial(material);
    root_node->AddChild(shape);

    if (view_) {
      view_->AddChild(*root_node);
      FX_LOGS(INFO) << "Child - outbound view is set up.";
    }

    root_node_ = std::move(root_node);
    FX_LOGS(INFO) << "Child - root node is set up.";
  }

  // Create frame to trigger on focus.
  {
    std::unique_ptr<scenic::EntityNode> frame = std::make_unique<scenic::EntityNode>(session);
    frame->SetLabel("child focus frame");

    const float kElevation = 110.f;  // Z height
    const float kBar = 50.f;
    const float kTranslateX = (kWidth - kBar) * 0.5f;
    const float kTranslateY = (kHeight - kBar) * 0.5f;

    scenic::Material material(session);
    material.SetColor(0, 0, 255, 255);  // Blue
    scenic::Rectangle horizontal_bar(session, kWidth, kBar);
    scenic::Rectangle vertical_bar(session, kBar, kHeight);

    scenic::ShapeNode top_bar(session);
    top_bar.SetTranslation(0.f, -kTranslateY, -kElevation);
    top_bar.SetShape(horizontal_bar);
    top_bar.SetMaterial(material);
    frame->AddChild(top_bar);

    scenic::ShapeNode bottom_bar(session);
    bottom_bar.SetTranslation(0.f, kTranslateY, -kElevation);
    bottom_bar.SetShape(horizontal_bar);
    bottom_bar.SetMaterial(material);
    frame->AddChild(bottom_bar);

    scenic::ShapeNode left_bar(session);
    left_bar.SetTranslation(-kTranslateX, 0, -kElevation);
    left_bar.SetShape(vertical_bar);
    left_bar.SetMaterial(material);
    frame->AddChild(left_bar);

    scenic::ShapeNode right_bar(session);
    right_bar.SetTranslation(kTranslateX, 0, -kElevation);
    right_bar.SetShape(vertical_bar);
    right_bar.SetMaterial(material);
    frame->AddChild(right_bar);

    focus_frame_ = std::move(frame);
    FX_LOGS(INFO) << "Child - focus frame prepared.";
  }

  // Create a visual tracker for pointer movement.
  {
    const float kElevation = 400.f;

    for (size_t i = 0; i < 10; ++i) {
      pointer_id_[i] = kNoFinger;

      pointer_tracker_[i] = std::make_unique<scenic::ShapeNode>(session);
      pointer_tracker_[i]->SetLabel("pointer tracker");
      pointer_tracker_[i]->SetTranslation(0.f, 0.f, -kElevation);

      scenic::Circle circle(session, 50.f);
      pointer_tracker_[i]->SetShape(circle);

      scenic::Material material(session);
      material.SetColor(0, 0, 255, 255);  // Blue
      pointer_tracker_[i]->SetMaterial(material);
    }

    FX_LOGS(INFO) << "Child - pointer tracker prepared.";
  }
}

}  // namespace direct_input_child
