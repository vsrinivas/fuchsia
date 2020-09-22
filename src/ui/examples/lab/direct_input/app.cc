// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/lab/direct_input/app.h"

#include <fuchsia/math/cpp/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fit/function.h>
#include <lib/fostr/fidl/fuchsia/ui/input/formatting.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/time.h>
#include <unistd.h>
#include <zircon/processargs.h>

#include <limits>
#include <string>
#include <utility>

namespace direct_input {

const uint32_t kNoFinger = std::numeric_limits<uint32_t>::max();  // Sentinel.

App::App(async::Loop* loop)
    : component_context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      message_loop_(loop),
      input_reader_(this),
      next_device_token_(0),
      focused_(false) {
  FX_DCHECK(component_context_);

  scenic_ = component_context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this](zx_status_t status) { OnScenicError(); });
  FX_LOGS(INFO) << "DirectInput - scenic connection";

  session_ = std::make_unique<scenic::Session>(scenic_.get());
  session_->SetDebugName("Direct Input");
  session_->set_error_handler([this](zx_status_t status) { this->OnSessionError(); });
  session_->set_event_handler(fit::bind_member(this, &App::OnSessionEvents));
  FX_LOGS(INFO) << "DirectInput - session set up";

  compositor_ = std::make_unique<scenic::DisplayCompositor>(session_.get());
  FX_LOGS(INFO) << "DirectInput - compositor set up";

  input_reader_.Start();
  component_context_->outgoing()->AddPublicService(
      input_device_registry_bindings_.GetHandler(this));
  FX_LOGS(INFO) << "DirectInput - input set up (Press ESC to quit).";

  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
    CreateScene(display_info.width_in_px, display_info.height_in_px);
    UpdateScene(zx_clock_get_monotonic());
  });

  {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
    launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
    launch_info.url =
        "fuchsia-pkg://fuchsia.com/direct_input_child#meta/"
        "direct_input_child.cmx";
    auto services = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);
    fuchsia::sys::LauncherSyncPtr launcher;
    component_context_->svc()->Connect(launcher.NewRequest());
    launcher->CreateComponent(std::move(launch_info), child_controller_.NewRequest());
    services->Connect(child_view_provider_.NewRequest());
  }

  {
    fuchsia::ui::input::SetHardKeyboardDeliveryCmd cmd;
    cmd.delivery_request = true;
    fuchsia::ui::input::Command input_cmd;
    input_cmd.set_set_hard_keyboard_delivery(std::move(cmd));
    session_->Enqueue(std::move(input_cmd));
  }

  FX_LOGS(INFO) << "DirectInput - child set up";
}

App::~App() { ReleaseSessionResources(); }

void App::ReleaseSessionResources() {
  if (session_) {
    compositor_.reset();
    camera_.reset();
    focus_frame_.reset();
    for (size_t i = 0; i < 10; ++i) {
      pointer_id_[i] = kNoFinger;
      pointer_tracker_[i].reset();
    }
    view_.reset();
    view_holder_.reset();
    child_view_holder_.reset();

    session_->Flush();
    session_.reset();
  }
}

void App::CheckQuit(const fuchsia::ui::input::InputEvent& event) {
  if (event.is_keyboard()) {
    fuchsia::ui::input::KeyboardEvent key_event = event.keyboard();
    if (key_event.hid_usage == /* Esc key */ 0x29 &&
        key_event.phase == fuchsia::ui::input::KeyboardEventPhase::RELEASED) {
      FX_LOGS(INFO) << "DirectInput - shutting down.";
      child_controller_->Kill();
      async::PostTask(message_loop_->dispatcher(), [this]() { OnSessionClose(); });
    }
  }
}

void App::OnScenicError() {
  FX_LOGS(ERROR) << "DirectInput - scenic connection error.";
  ReleaseSessionResources();
  message_loop_->Quit();
}

void App::OnSessionError() {
  FX_LOGS(ERROR) << "DirectInput - session error.";
  ReleaseSessionResources();
  message_loop_->Quit();
}

void App::OnSessionClose() {
  FX_LOGS(INFO) << "DirectInput - session close.";
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
      FX_LOGS(ERROR) << "DirectInput - GFX command unimplemented.";
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

void App::UpdateScene(uint64_t next_presentation_time) {
  session_->Present(next_presentation_time, [this](fuchsia::images::PresentationInfo info) {
    UpdateScene(info.presentation_time + 2 * info.presentation_interval);
  });
}

void App::CreateScene(float display_width, float display_height) {
  FX_LOGS(INFO) << "DirectInput - display size: " << display_width << ", " << display_height;

  const float kMargin = 100.f;
  const float kRootWidth = display_width - 2.f * kMargin;
  const float kRootHeight = display_height - 2.f * kMargin;

  width_in_px_ = display_width;  // Store display size, not view size!
  height_in_px_ = display_height;

  auto session = session_.get();

  scenic::LayerStack layer_stack(session);
  compositor_->SetLayerStack(layer_stack);

  // Set up scene.
  scenic::Scene scene(session);
  {
    camera_ = std::make_unique<scenic::Camera>(scene);
    scenic::Renderer renderer(session);
    renderer.SetCamera(camera_->id());

    scenic::Layer layer(session);
    layer.SetRenderer(renderer);
    layer.SetSize(display_width, display_height);  // Need screen size, SCN-248.
    layer_stack.AddLayer(layer);

    scenic::AmbientLight ambient_light(session);
    ambient_light.SetColor(0.3f, 0.3f, 0.3f);
    scene.AddLight(ambient_light);

    scenic::DirectionalLight directional_light(session);
    directional_light.SetColor(0.7f, 0.7f, 0.7f);
    directional_light.SetDirection(1.f, 1.f, -2.f);
    scene.AddLight(directional_light);

    FX_LOGS(INFO) << "DirectInput - scene is set up.";
  }

  // Set up root node, its dimensions, add green background.
  scenic::EntityNode root_node(session);
  {
    const float kElevation = 10.f;

    root_node.SetLabel("root_node");
    root_node.SetClip(0, true);
    root_node.SetTranslation(display_width * 0.5f, display_height * 0.5f, -kElevation);

    scenic::ShapeNode background(session);
    scenic::Rectangle shape(session, kRootWidth, kRootHeight);
    background.SetShape(shape);
    scenic::Material material(session);
    material.SetColor(0, 255, 0, 255);  // Green
    background.SetMaterial(material);
    root_node.AddChild(background);

    scene.AddChild(root_node);
    FX_LOGS(INFO) << "DirectInput - root node is set up.";
  }

  // Create View/ViewHolder. Attach ViewHolder to root node.
  {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

    view_holder_ =
        std::make_unique<scenic::ViewHolder>(session, std::move(view_holder_token), "view_holder");
    view_holder_->SetLabel("main view_holder");
    view_ = std::make_unique<scenic::View>(session, std::move(view_token), "view");
    view_->SetLabel("main view");

    root_node.Attach(*view_holder_);
    FX_LOGS(INFO) << "DirectInput - View/ViewHolder pair created.";
  }

  const float kMainWidth = display_width - 4.f * kMargin;
  const float kMainHeight = display_height - 4.f * kMargin;

  // Create main node, attach to View. Main node is accessible only from View.
  {
    const float kElevation = 20.f;

    scenic::ShapeNode node(session);
    node.SetLabel("main node");
    scenic::Rectangle shape(session, kMainWidth, kMainHeight);
    node.SetShape(shape);
    scenic::Material material(session);
    material.SetColor(255, 0, 255, 255);  // Fuchsia
    node.SetMaterial(material);
    node.SetTranslation(0.f, 0.f, -kElevation);
    view_->AddChild(node);
    FX_LOGS(INFO) << "DirectInput - main node added to view.";
  }

  // Create frame to trigger on focus to main node.
  {
    std::unique_ptr<scenic::EntityNode> frame = std::make_unique<scenic::EntityNode>(session);
    frame->SetLabel("focus frame");

    const float kElevation = 30.f;  // Z height
    const float kBar = 50.f;        // bar thickness
    const float kTranslateX = (kMainWidth - kBar) * 0.5f;
    const float kTranslateY = (kMainHeight - kBar) * 0.5f;

    scenic::Material material(session);
    material.SetColor(128, 0, 128, 255);  // Purple
    scenic::Rectangle horizontal_bar(session, kMainWidth, kBar);
    scenic::Rectangle vertical_bar(session, kBar, kMainHeight);

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
    FX_LOGS(INFO) << "DirectInput - focus frame prepared.";
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
      material.SetColor(128, 0, 128, 255);  // Purple
      pointer_tracker_[i]->SetMaterial(material);
    }

    FX_LOGS(INFO) << "DirectInput - pointer tracker prepared.";
  }

  // Connect to child view, put it in a ViewHolder.
  {
    zx::eventpair view_holder;
    zx::eventpair view;
    zx_status_t status = zx::eventpair::create(0u, &view_holder, &view);
    FX_CHECK(status == ZX_OK);
    child_view_provider_->CreateView(std::move(view), nullptr, nullptr);

    child_view_holder_ =
        std::make_unique<scenic::ViewHolder>(session, std::move(view_holder), "child view holder");
    child_view_holder_->SetLabel("child_view_holder");

    root_node.Attach(*child_view_holder_);
    FX_LOGS(INFO) << "DirectInput - child view requested, view holder set up.";
  }
}

void App::OnDeviceSensorEvent(uint32_t device_id, fuchsia::ui::input::InputReport event) {
  FX_VLOGS(3) << "DirectInput - OnDeviceSensorEvent(device_id=" << device_id << "): " << event;
}

void App::OnDeviceInputEvent(uint32_t compositor_id, fuchsia::ui::input::InputEvent event) {
  FX_VLOGS(1) << "DirectInput - OnDeviceInputEvent: " << event;

  CheckQuit(event);

  fuchsia::ui::input::Command command;
  if (event.is_pointer()) {
    // Pointer events are tied to a particular compositor for routing.
    fuchsia::ui::input::SendPointerInputCmd cmd;
    cmd.pointer_event = std::move(event.pointer());
    cmd.compositor_id = compositor_id;
    command.set_send_pointer_input(std::move(cmd));
  } else if (event.is_keyboard()) {
    // Keyboard events are sent to a focused view, wherever that may be.
    fuchsia::ui::input::SendKeyboardInputCmd cmd;
    cmd.keyboard_event = std::move(event.keyboard());
    cmd.compositor_id = compositor_id;
    command.set_send_keyboard_input(std::move(cmd));
  }
  session_->Enqueue(std::move(command));
}

void App::RegisterDevice(
    fuchsia::ui::input::DeviceDescriptor descriptor,
    fidl::InterfaceRequest<fuchsia::ui::input::InputDevice> input_device_request) {
  uint32_t device_id = ++next_device_token_;

  FX_VLOGS(2) << "DirectInput - RegisterDevice: " << device_id << " " << descriptor;
  std::unique_ptr<ui_input::InputDeviceImpl> input_device =
      std::make_unique<ui_input::InputDeviceImpl>(device_id, std::move(descriptor),
                                                  std::move(input_device_request), this);

  std::unique_ptr<ui_input::DeviceState> state;
  if (input_device->descriptor()->sensor) {
    ui_input::OnSensorEventCallback callback = [this](uint32_t device_id,
                                                      fuchsia::ui::input::InputReport event) {
      OnDeviceSensorEvent(device_id, std::move(event));
    };
    state = std::make_unique<ui_input::DeviceState>(input_device->id(), input_device->descriptor(),
                                                    std::move(callback));
  } else {
    uint32_t compositor_id = compositor_->id();  // Input destination.
    ui_input::OnEventCallback callback = [this,
                                          compositor_id](fuchsia::ui::input::InputEvent event) {
      OnDeviceInputEvent(compositor_id, std::move(event));
    };
    state = std::make_unique<ui_input::DeviceState>(input_device->id(), input_device->descriptor(),
                                                    std::move(callback));
  }

  state->OnRegistered();
  device_by_id_.emplace(device_id, std::move(input_device));
  device_state_by_id_.emplace(device_id, std::move(state));
}

void App::OnDeviceDisconnected(ui_input::InputDeviceImpl* input_device) {
  const uint32_t device_id = input_device->id();

  if (device_by_id_.count(device_id) == 0)
    return;

  FX_VLOGS(2) << "DirectInput - UnregisterDevice: " << device_id;

  device_state_by_id_[device_id]->OnUnregistered();
  device_state_by_id_.erase(device_id);
  device_by_id_.erase(device_id);
}

void App::OnReport(ui_input::InputDeviceImpl* input_device,
                   fuchsia::ui::input::InputReport report) {
  const uint32_t device_id = input_device->id();

  if (device_by_id_.count(device_id) == 0)
    return;

  FX_VLOGS(3) << "DirectInput - OnReport: " << device_id << " " << report;

  ui_input::DeviceState* state = device_state_by_id_[device_id].get();
  fuchsia::math::Size size;
  size.width = width_in_px_;
  size.height = height_in_px_;
  state->Update(std::move(report), size);
}

}  // namespace direct_input
