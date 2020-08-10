// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include <cmath>

class BouncingBallView : public fuchsia::ui::scenic::SessionListener {
 public:
  BouncingBallView(sys::ComponentContext* component_context,
                   fuchsia::ui::views::ViewToken view_token,
                   fuchsia::ui::views::ViewRefControl view_ref_control,
                   fuchsia::ui::views::ViewRef view_ref)
      : session_listener_binding_(this) {
    // Connect to Scenic.
    fuchsia::ui::scenic::ScenicPtr scenic =
        component_context->svc()->Connect<fuchsia::ui::scenic::Scenic>();

    // Create a Scenic Session and a Scenic SessionListener.
    scenic->CreateSession(session_.NewRequest(), session_listener_binding_.NewBinding());

    InitializeScene(std::move(view_token), std::move(view_ref_control), std::move(view_ref));
  }

 private:
  static void PushCommand(std::vector<fuchsia::ui::scenic::Command>* cmds,
                          fuchsia::ui::gfx::Command cmd) {
    // Wrap the gfx::Command in a scenic::Command, then push it.
    cmds->push_back(scenic::NewCommand(std::move(cmd)));
  }

  void InitializeScene(fuchsia::ui::views::ViewToken view_token,
                       fuchsia::ui::views::ViewRefControl view_ref_control,
                       fuchsia::ui::views::ViewRef view_ref) {
    // Build up a list of commands we will send over our Scenic Session.
    std::vector<fuchsia::ui::scenic::Command> cmds;

    // View: Use |view_token| to create a View in the Session.
    PushCommand(
        &cmds, scenic::NewCreateViewCmd(kViewId, std::move(view_token), std::move(view_ref_control),
                                        std::move(view_ref), "bouncing_circle_view"));

    // Root Node.
    PushCommand(&cmds, scenic::NewCreateEntityNodeCmd(kRootNodeId));
    PushCommand(&cmds, scenic::NewAddChildCmd(kViewId, kRootNodeId));

    // Background Material.
    PushCommand(&cmds, scenic::NewCreateMaterialCmd(kBgMaterialId));
    PushCommand(&cmds, scenic::NewSetColorCmd(kBgMaterialId, 0xf5, 0x00, 0x57,
                                              0xff));  // Pink A400

    // Background ShapeNode.
    PushCommand(&cmds, scenic::NewCreateShapeNodeCmd(kBgNodeId));
    PushCommand(&cmds, scenic::NewSetMaterialCmd(kBgNodeId, kBgMaterialId));
    PushCommand(&cmds, scenic::NewAddChildCmd(kRootNodeId, kBgNodeId));

    // Circle's Material.
    PushCommand(&cmds, scenic::NewCreateMaterialCmd(kCircleMaterialId));
    PushCommand(&cmds, scenic::NewSetColorCmd(kCircleMaterialId, 0x67, 0x3a, 0xb7,
                                              0xff));  // Deep Purple 500

    // Circle's ShapeNode.
    PushCommand(&cmds, scenic::NewCreateShapeNodeCmd(kCircleNodeId));
    PushCommand(&cmds, scenic::NewSetMaterialCmd(kCircleNodeId, kCircleMaterialId));
    PushCommand(&cmds, scenic::NewAddChildCmd(kRootNodeId, kCircleNodeId));

    session_->Enqueue(std::move(cmds));

    // Apply all the commands we've enqueued by calling Present. For this first
    // frame we call Present with a presentation_time = 0 which means it the
    // commands should be applied immediately. For future frames, we'll use the
    // timing information we receive to have precise presentation times.
    session_->Present(
        0, {}, {}, [this](fuchsia::images::PresentationInfo info) { OnPresent(std::move(info)); });
  }

  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicError(std::string error) override {}

  static bool IsViewPropertiesChangedEvent(const fuchsia::ui::scenic::Event& event) {
    return event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
           event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged;
  }

  static bool IsPointerEvent(const fuchsia::ui::scenic::Event& event) {
    return event.Which() == fuchsia::ui::scenic::Event::Tag::kInput &&
           event.input().Which() == fuchsia::ui::input::InputEvent::Tag::kPointer;
  }

  static bool IsPointerDownEvent(const fuchsia::ui::scenic::Event& event) {
    return IsPointerEvent(event) &&
           event.input().pointer().phase == fuchsia::ui::input::PointerEventPhase::DOWN;
  }

  static bool IsPointerUpEvent(const fuchsia::ui::scenic::Event& event) {
    return IsPointerEvent(event) &&
           event.input().pointer().phase == fuchsia::ui::input::PointerEventPhase::UP;
  }

  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
    for (auto& event : events) {
      if (IsViewPropertiesChangedEvent(event)) {
        OnViewPropertiesChanged(event.gfx().view_properties_changed().properties);
      } else if (IsPointerDownEvent(event)) {
        pointer_down_ = true;
        pointer_id_ = event.input().pointer().pointer_id;
      } else if (IsPointerUpEvent(event)) {
        if (pointer_id_ == event.input().pointer().pointer_id) {
          pointer_down_ = false;
        }
      } else {
        // Unhandled event.
      }
    }
  }

  void OnViewPropertiesChanged(fuchsia::ui::gfx::ViewProperties vp) {
    view_width_ = (vp.bounding_box.max.x - vp.inset_from_max.x) -
                  (vp.bounding_box.min.x + vp.inset_from_min.x);
    view_height_ = (vp.bounding_box.max.y - vp.inset_from_max.y) -
                   (vp.bounding_box.min.y + vp.inset_from_min.y);

    // Position is relative to the View's origin system.
    const float center_x = view_width_ * .5f;
    const float center_y = view_height_ * .5f;

    // Build up a list of commands we will send over our Scenic Session.
    std::vector<fuchsia::ui::scenic::Command> cmds;

    // Background Shape.
    const int bg_shape_id = new_resource_id_++;
    PushCommand(&cmds, scenic::NewCreateRectangleCmd(bg_shape_id, view_width_, view_height_));
    PushCommand(&cmds, scenic::NewSetShapeCmd(kBgNodeId, bg_shape_id));

    // We release the Shape Resource here, but it continues to stay alive in
    // Scenic because it's being referenced by background ShapeNode (i.e. the
    // one with id kBgNodeId). However, we no longer have a way to reference it.
    //
    // Once the background ShapeNode no longer references this shape, because a
    // new Shape was set on it, this Shape will be destroyed internally in
    // Scenic.
    PushCommand(&cmds, scenic::NewReleaseResourceCmd(bg_shape_id));

    // Translate the background node.
    constexpr float kBackgroundElevation = 0.f;
    PushCommand(&cmds, scenic::NewSetTranslationCmd(kBgNodeId,
                                                    {center_x, center_y, -kBackgroundElevation}));

    // Circle Shape.
    circle_radius_ = std::min(view_width_, view_height_) * .1f;
    const int circle_shape_id = new_resource_id_++;
    PushCommand(&cmds, scenic::NewCreateCircleCmd(circle_shape_id, circle_radius_));
    PushCommand(&cmds, scenic::NewSetShapeCmd(kCircleNodeId, circle_shape_id));

    // We release the Shape Resource here, but it continues to stay alive in
    // Scenic because it's being referenced by circle's ShapeNode (i.e. the one
    // with id kCircleNodeId). However, we no longer have a way to reference it.
    //
    // Once the background ShapeNode no longer references this shape, because a
    // new Shape was set on it, this Shape will be destroyed internally in
    // Scenic.
    PushCommand(&cmds, scenic::NewReleaseResourceCmd(circle_shape_id));

    session_->Enqueue(std::move(cmds));

    // The commands won't actually get committed until Session.Present() is
    // called. However, since we're animating every frame, in this case we can
    // assume Present() will be called shortly.
  }

  void UpdateCirclePosition(float t) {
    if (pointer_down_) {
      // Move back to near initial position and velocity when there's a pointer
      // down event.
      circle_pos_x_ = initialCirclePosX;
      circle_pos_y_ = initialCirclePosY;
      circle_velocity_y_ = 0.f;
      return;
    }
    constexpr float kYAcceleration = 3.f;
    circle_velocity_y_ += kYAcceleration * t;

    constexpr float kCircleVelocityX = 0.2;
    circle_pos_x_ += kCircleVelocityX * t;
    circle_pos_y_ += fmin(circle_velocity_y_ * t, 1.f);

    if (circle_pos_y_ > 1.f) {
      // Bounce.
      circle_velocity_y_ *= -0.8f;
      circle_pos_y_ = 1.f;
    }
    if (circle_pos_y_ >= 0.999f && fabs(circle_velocity_y_) < .015f) {
      // If the circle stops bouncing, start the simulation again.
      circle_pos_y_ = 0.f;
      circle_velocity_y_ = 0.f;
    }
    if (circle_pos_x_ > 1) {
      // Wrap the x position.
      circle_pos_x_ = fmod(circle_pos_x_, 1.f);
    }
  }

  void OnPresent(fuchsia::images::PresentationInfo presentation_info) {
    uint64_t presentation_time = presentation_info.presentation_time;

    constexpr float kSecondsPerNanosecond = .000'000'001f;
    float t = (presentation_time - last_presentation_time_) * kSecondsPerNanosecond;
    if (last_presentation_time_ == 0) {
      t = 0;
    }
    last_presentation_time_ = presentation_time;

    std::vector<fuchsia::ui::scenic::Command> cmds;

    UpdateCirclePosition(t);
    const float circle_pos_x_absolute = circle_pos_x_ * view_width_;
    const float circle_pos_y_absolute = circle_pos_y_ * view_height_ - circle_radius_;

    // Translate the circle's node.
    constexpr float kCircleElevation = 8.f;
    PushCommand(&cmds, scenic::NewSetTranslationCmd(
                           kCircleNodeId,
                           {circle_pos_x_absolute, circle_pos_y_absolute, -kCircleElevation}));
    session_->Enqueue(std::move(cmds));

    zx_time_t next_presentation_time =
        presentation_info.presentation_time + presentation_info.presentation_interval;
    session_->Present(
        next_presentation_time, {}, {},
        [this](fuchsia::images::PresentationInfo info) { OnPresent(std::move(info)); });
  }

  const int kViewId = 1;
  const int kRootNodeId = 2;
  const int kBgMaterialId = 3;
  const int kBgNodeId = 4;
  const int kCircleMaterialId = 5;
  const int kCircleNodeId = 6;

  // For other resources we create, we use |new_resource_id_| and then increment
  // it.
  int new_resource_id_ = 7;

  uint64_t last_presentation_time_ = 0;

  float view_width_ = 0;
  float view_height_ = 0;

  // Position is in the range [0, 1] and then multiplied by (view_width_,
  // view_height_).
  static constexpr float initialCirclePosX = 0.12f;
  static constexpr float initialCirclePosY = 0.26f;
  float circle_pos_x_ = initialCirclePosX;
  float circle_pos_y_ = initialCirclePosY;

  float circle_velocity_y_ = 0.f;

  // Circle's radius in logical pixels.
  float circle_radius_ = 0.f;

  // Input.
  bool pointer_down_ = false;
  uint32_t pointer_id_ = 0;

  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_binding_;
  fuchsia::ui::scenic::SessionPtr session_;
};

// Implement the ViewProvider interface, a standard way for an embedder to
// provide us a token that, using Scenic APIs, allows us to create a View
// that's attached to the embedder's ViewHolder.
class ViewProviderService : public fuchsia::ui::app::ViewProvider {
 public:
  ViewProviderService(sys::ComponentContext* component_context)
      : component_context_(component_context) {}

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override {
    auto [control_ref, view_ref] = scenic::ViewRefPair::New();
    CreateViewWithViewRef(std::move(view_token), std::move(control_ref), std::move(view_ref));
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair view_token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override {
    auto view = std::make_unique<BouncingBallView>(
        component_context_, scenic::ToViewToken(std::move(view_token)), std::move(view_ref_control),
        std::move(view_ref));
    views_.push_back(std::move(view));
  }

  void HandleViewProviderRequest(fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  sys::ComponentContext* component_context_ = nullptr;
  std::vector<std::unique_ptr<BouncingBallView>> views_;
  fidl::BindingSet<ViewProvider> bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  std::unique_ptr<sys::ComponentContext> component_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  ViewProviderService view_provider(component_context.get());

  // Add our ViewProvider service to the outgoing services.
  component_context->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      [&view_provider](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        view_provider.HandleViewProviderRequest(std::move(request));
      });

  loop.Run();
  return 0;
}
