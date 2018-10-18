// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/base_view/cpp/base_view.h"

#include "lib/fxl/logging.h"
#include "lib/ui/gfx/cpp/math.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic {

BaseView::BaseView(ViewContext context, const std::string& debug_name)
    : startup_context_(context.startup_context),
      listener_binding_(this,
                        std::move(context.session_and_listener_request.second)),
      session_(std::move(context.session_and_listener_request.first)),
      view_(&session_, std::move(context.view_token), debug_name) {
  // We must immediately invalidate the scene, otherwise we wouldn't ever hook
  // the View up to the ViewHolder.  An alternative would be to require
  // subclasses to call an Init() method to set up the initial connection.
  InvalidateScene();
}

void BaseView::SetReleaseHandler(fit::closure callback) {
  listener_binding_.set_error_handler(std::move(callback));
}

BaseView::EmbeddedViewInfo BaseView::LaunchComponentAndCreateView(
    std::string component_url, std::vector<std::string> component_args) {
  auto& launcher = startup_context_->launcher();
  FXL_DCHECK(launcher) << "no Launcher available.";

  zx::eventpair view_holder_token, view_token;
  auto status = zx::eventpair::create(0u, &view_holder_token, &view_token);
  FXL_DCHECK(status == ZX_OK) << "failed to create tokens.";

  EmbeddedViewInfo info;
  info.view_holder_token = std::move(view_holder_token);

  launcher->CreateComponent(
      {.url = component_url,
       .arguments = fidl::VectorPtr(std::vector<fidl::StringPtr>(
           component_args.begin(), component_args.end())),
       .directory_request = info.app_services.NewRequest()},
      info.controller.NewRequest());

  info.app_services.ConnectToService(info.view_provider.NewRequest());

  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> services_to_child_view;
  info.services_to_child_view = services_to_child_view.NewRequest();

  info.view_provider->CreateView(std::move(view_token),
                                 info.services_from_child_view.NewRequest(),
                                 std::move(services_to_child_view));

  return info;
}

void BaseView::InvalidateScene() {
  if (invalidate_pending_)
    return;

  invalidate_pending_ = true;

  // Present the scene ASAP. Pass in the last presentation time; otherwise, if
  // presentation_time argument is less than the previous time passed to
  // PresentScene, the Session will be closed.
  // (We cannot use the current time because the last requested presentation
  // time, |last_presentation_time_|, could still be in the future. This is
  // because Session.Present() returns after it _begins_ preparing the given
  // frame, not after it is presented.)
  if (!present_pending_)
    PresentScene(last_presentation_time_);
}

void BaseView::OnScenicEvent(
    fidl::VectorPtr<fuchsia::ui::scenic::Event> events) {
  for (auto& event : *events) {
    switch (event.Which()) {
      case ::fuchsia::ui::scenic::Event::Tag::kGfx:
        switch (event.gfx().Which()) {
          case ::fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged: {
            auto& evt = event.gfx().view_properties_changed();
            FXL_DCHECK(view_.id() == evt.view_id);
            auto old_props = view_properties_;
            view_properties_ = evt.properties;

            ::fuchsia::ui::gfx::BoundingBox layout_box =
                ViewPropertiesLayoutBox(view_properties_);

            logical_size_ = scenic::Max(layout_box.max - layout_box.min, 0.f);

            OnPropertiesChanged(std::move(old_props));
            break;
          }
          default: {
            OnScenicEvent(std::move(event));
          }
        }
        break;
      case ::fuchsia::ui::scenic::Event::Tag::kInput: {
        OnInputEvent(std::move(event.input()));
        break;
      }
      case ::fuchsia::ui::scenic::Event::Tag::kUnhandled: {
        OnUnhandledCommand(std::move(event.unhandled()));
        break;
      }
      default: {
        OnScenicEvent(std::move(event));
      }
    }
  }
}

void BaseView::PresentScene(zx_time_t presentation_time) {
  FXL_DCHECK(!present_pending_);

  present_pending_ = true;

  // Keep track of the most recent presentation time we've passed to
  // Session.Present(), for use in InvalidateScene().
  last_presentation_time_ = presentation_time;

  session()->Present(presentation_time,
                     [this](fuchsia::images::PresentationInfo info) {
                       FXL_DCHECK(present_pending_);

                       zx_time_t next_presentation_time =
                           info.presentation_time + info.presentation_interval;

                       bool present_needed = false;
                       if (invalidate_pending_) {
                         invalidate_pending_ = false;
                         OnSceneInvalidated(std::move(info));
                         present_needed = true;
                       }

                       present_pending_ = false;
                       if (present_needed)
                         PresentScene(next_presentation_time);
                     });
}

}  // namespace scenic
