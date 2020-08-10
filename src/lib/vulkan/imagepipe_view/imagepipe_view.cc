// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imagepipe_view.h"

#include <lib/syslog/global.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

enum {
  kViewId = 1,
  kRootNodeId = 2,
  kMaterialId = 3,
  kShapeNodeId = 4,
  kImagePipeId = 5,
  kFirstNewResourceId = 6,
};

std::unique_ptr<ImagePipeView> ImagePipeView::Create(sys::ComponentContext* context,
                                                     fuchsia::ui::views::ViewToken view_token,
                                                     fuchsia::ui::views::ViewRefControl control_ref,
                                                     fuchsia::ui::views::ViewRef view_ref,
                                                     ResizeCallback resize_callback) {
  auto view = std::make_unique<ImagePipeView>(std::move(resize_callback));
  if (!view)
    return nullptr;
  if (!view->Init(context, std::move(view_token), std::move(control_ref), std::move(view_ref)))
    return nullptr;
  return view;
}

ImagePipeView::ImagePipeView(ResizeCallback resize_callback)
    : session_listener_binding_(this),
      resize_callback_(std::move(resize_callback)),
      new_resource_id_(kFirstNewResourceId) {}

static void PushCommand(std::vector<fuchsia::ui::scenic::Command>* cmds,
                        fuchsia::ui::gfx::Command cmd) {
  // Wrap the gfx::Command in a scenic::Command, then push it.
  cmds->push_back(scenic::NewCommand(std::move(cmd)));
}

bool ImagePipeView::Init(sys::ComponentContext* context, fuchsia::ui::views::ViewToken view_token,
                         fuchsia::ui::views::ViewRefControl control_ref,
                         fuchsia::ui::views::ViewRef view_ref) {
  fuchsia::ui::scenic::ScenicPtr scenic = context->svc()->Connect<fuchsia::ui::scenic::Scenic>();

  scenic->CreateSession(session_.NewRequest(), session_listener_binding_.NewBinding());

  zx::channel remote_endpoint;
  zx_status_t status = zx::channel::create(0, &image_pipe_endpoint_, &remote_endpoint);
  if (status != ZX_OK) {
    FX_LOGF(ERROR, "ImagePipeView", "Init: failed to create channel (%d)", status);
    return false;
  }

  std::vector<fuchsia::ui::scenic::Command> cmds;

  PushCommand(&cmds,
              scenic::NewCreateViewCmd(kViewId, std::move(view_token), std::move(control_ref),
                                       std::move(view_ref), "imagepipe_view"));
  PushCommand(&cmds, scenic::NewCreateEntityNodeCmd(kRootNodeId));
  PushCommand(&cmds, scenic::NewAddChildCmd(kViewId, kRootNodeId));
  PushCommand(&cmds, scenic::NewCreateMaterialCmd(kMaterialId));
  PushCommand(&cmds, scenic::NewCreateImagePipe2Cmd(
                         kImagePipeId, fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(
                                           std::move(remote_endpoint))));
  PushCommand(&cmds, scenic::NewSetTextureCmd(kMaterialId, kImagePipeId));
  PushCommand(&cmds, scenic::NewCreateShapeNodeCmd(kShapeNodeId));
  PushCommand(&cmds, scenic::NewSetMaterialCmd(kShapeNodeId, kMaterialId));
  PushCommand(&cmds, scenic::NewAddChildCmd(kRootNodeId, kShapeNodeId));

  session_->Enqueue(std::move(cmds));
  session_->Present(0,                                             // presentation time
                    {},                                            // acquire fences
                    {},                                            // release fences
                    [](fuchsia::images::PresentationInfo info) {}  // presentation callback
  );
  return true;
}

static bool IsViewPropertiesChangedEvent(const fuchsia::ui::scenic::Event& event) {
  return (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx) &&
         (event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged);
}

// |fuchsia::ui::scenic::SessionListener|
void ImagePipeView::OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) {
  for (auto& event : events) {
    if (IsViewPropertiesChangedEvent(event)) {
      OnViewPropertiesChanged(event.gfx().view_properties_changed().properties);
    }
  }
}

void ImagePipeView::OnViewPropertiesChanged(fuchsia::ui::gfx::ViewProperties vp) {
  view_width_ =
      (vp.bounding_box.max.x - vp.inset_from_max.x) - (vp.bounding_box.min.x + vp.inset_from_min.x);
  view_height_ =
      (vp.bounding_box.max.y - vp.inset_from_max.y) - (vp.bounding_box.min.y + vp.inset_from_min.y);

  if (view_width_ == 0 || view_height_ == 0)
    return;

  std::vector<fuchsia::ui::scenic::Command> cmds;

  int shape_id = new_resource_id_++;
  PushCommand(&cmds, scenic::NewCreateRectangleCmd(shape_id, view_width_, view_height_));
  PushCommand(&cmds, scenic::NewSetShapeCmd(kShapeNodeId, shape_id));
  PushCommand(&cmds, scenic::NewReleaseResourceCmd(shape_id));

  // Position is relative to the View's origin system.
  const float center_x = view_width_ * .5f;
  const float center_y = view_height_ * .5f;

  constexpr float kBackgroundElevation = 0.f;
  PushCommand(&cmds, scenic::NewSetTranslationCmd(kShapeNodeId,
                                                  {center_x, center_y, -kBackgroundElevation}));

  session_->Enqueue(std::move(cmds));
  session_->Present(0,                                             // presentation time
                    {},                                            // acquire fences
                    {},                                            // release fences
                    [](fuchsia::images::PresentationInfo info) {}  // presentation callback
  );
  resize_callback_(view_width_, view_height_);
}

// |fuchsia::ui::scenic::SessionListener|
void ImagePipeView::OnScenicError(std::string error) {
  FX_LOGF(ERROR, "ImagePipeView", "OnScenicError: %s", error.c_str());
}

ImagePipeViewProviderService::ImagePipeViewProviderService(sys::ComponentContext* context,
                                                           CreateViewCallback create_view_callback)
    : create_view_callback_(std::move(create_view_callback)) {
  context->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        this->HandleViewProviderRequest(std::move(request));
      });
}

void ImagePipeViewProviderService::CreateView(
    zx::eventpair view_token,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  auto [view_ref_control, view_ref] = scenic::ViewRefPair::New();
  CreateViewWithViewRef(std::move(view_token), std::move(view_ref_control), std::move(view_ref));
}

void ImagePipeViewProviderService::CreateViewWithViewRef(
    zx::eventpair view_token, fuchsia::ui::views::ViewRefControl view_ref_control,
    fuchsia::ui::views::ViewRef view_ref) {
  create_view_callback_(scenic::ToViewToken(std::move(view_token)), std::move(view_ref_control),
                        std::move(view_ref));
}

void ImagePipeViewProviderService::HandleViewProviderRequest(
    fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}
