// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flatland_view.h"

#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>

const fuchsia::ui::composition::TransformId kRootTransform = {1};
const fuchsia::ui::composition::ContentId kViewport = {1};

// static
std::unique_ptr<FlatlandView> FlatlandView::Create(
    sys::ComponentContext* context, fuchsia::ui::views::ViewCreationToken view_creation_token,
    ResizeCallback resize_callback) {
  auto view = std::make_unique<FlatlandView>(std::move(resize_callback));
  if (!view)
    return nullptr;
  if (!view->Init(context, std::move(view_creation_token)))
    return nullptr;
  return view;
}

FlatlandView::FlatlandView(ResizeCallback resize_callback)
    : resize_callback_(std::move(resize_callback)) {}

bool FlatlandView::Init(sys::ComponentContext* context,
                        fuchsia::ui::views::ViewCreationToken view_creation_token) {
  context->svc()->Connect<>(flatland_.NewRequest());
  flatland_->SetDebugName("FlatlandView");
  flatland_.events().OnError = fit::bind_member(this, &FlatlandView::OnError);
  flatland_.events().OnNextFrameBegin = fit::bind_member(this, &FlatlandView::OnNextFrameBegin);

  flatland_->CreateTransform(kRootTransform);
  flatland_->SetRootTransform(kRootTransform);
  flatland_->CreateView2(std::move(view_creation_token), scenic::NewViewIdentityOnCreation(), {},
                         parent_viewport_watcher_.NewRequest());
  parent_viewport_watcher_->GetLayout(fit::bind_member(this, &FlatlandView::OnGetLayout));

  zx::channel::create(0, &viewport_creation_token_, &child_view_creation_token_);

  return true;
}

void FlatlandView::OnGetLayout(fuchsia::ui::composition::LayoutInfo info) {
  resize_callback_(info.logical_size().width, info.logical_size().height);

  fuchsia::ui::composition::ViewportProperties properties;
  properties.set_logical_size(info.logical_size());
  if (viewport_creation_token_.is_valid()) {
    // The first time that we receive layout information, create a viewport
    // using the token that was stashed during Init(). External code will attach a view to this
    // viewport via the token obtained from TakeChildViewCreationToken().
    fuchsia::ui::views::ViewportCreationToken viewport_creation_token;
    viewport_creation_token.value = std::move(viewport_creation_token_);
    fidl::InterfacePtr<fuchsia::ui::composition::ChildViewWatcher> child_view_watcher;
    flatland_->CreateViewport(kViewport, std::move(viewport_creation_token), std::move(properties),
                              child_view_watcher.NewRequest());
    flatland_->SetContent(kRootTransform, kViewport);
  } else {
    flatland_->SetViewportProperties(kViewport, std::move(properties));
  }

  Present();
  parent_viewport_watcher_->GetLayout(fit::bind_member(this, &FlatlandView::OnGetLayout));
}

void FlatlandView::OnError(fuchsia::ui::composition::FlatlandError error) {
  FX_SLOG(ERROR, "FlatlandError", KV("tag", "FlatlandView"), KV("error", static_cast<int>(error)));
}

void FlatlandView::Present() {
  if (present_credits_ == 0) {
    pending_present_ = true;
    return;
  }
  --present_credits_;
  fuchsia::ui::composition::PresentArgs present_args;
  present_args.set_requested_presentation_time(0);
  present_args.set_acquire_fences({});
  present_args.set_release_fences({});
  present_args.set_unsquashable(false);
  flatland_->Present(std::move(present_args));
}

void FlatlandView::OnNextFrameBegin(fuchsia::ui::composition::OnNextFrameBeginValues values) {
  present_credits_ += values.additional_present_credits();
  if (present_credits_ > 0 && pending_present_) {
    Present();
    pending_present_ = false;
  }
}

FlatlandViewProviderService::FlatlandViewProviderService(sys::ComponentContext* context,
                                                         CreateView2Callback create_view_callback)
    : create_view_callback_(std::move(create_view_callback)) {
  context->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        this->HandleViewProviderRequest(std::move(request));
      });
}

void FlatlandViewProviderService::CreateView(
    zx::eventpair view_token,
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) {
  FX_NOTIMPLEMENTED() << "Only Flatland is supported. This is a Gfx ViewProvider method.";
}

void FlatlandViewProviderService::CreateViewWithViewRef(
    zx::eventpair view_token, fuchsia::ui::views::ViewRefControl view_ref_control,
    fuchsia::ui::views::ViewRef view_ref) {
  FX_NOTIMPLEMENTED() << "Only Flatland is supported. This is a Gfx ViewProvider method.";
}

void FlatlandViewProviderService::CreateView2(fuchsia::ui::app::CreateView2Args args) {
  create_view_callback_(std::move(args));
}

void FlatlandViewProviderService::HandleViewProviderRequest(
    fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}
