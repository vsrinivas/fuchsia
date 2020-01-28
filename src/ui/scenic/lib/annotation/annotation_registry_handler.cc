// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/annotation/annotation_registry_handler.h"

#include "src/ui/scenic/lib/gfx/engine/annotation_manager.h"

namespace scenic_impl {

AnnotationRegistryHandler::AnnotationRegistryHandler(fidl::InterfaceRequest<Registry> request,
                                                     AnnotationHandlerId handler_id,
                                                     gfx::AnnotationManager *annotation_manager)
    : id_(handler_id), binding_(this, std::move(request)) {
  if (annotation_manager) {
    InitializeWithGfxAnnotationManager(annotation_manager);
  }
  binding_.set_error_handler(fit::bind_member(this, &AnnotationRegistryHandler::ErrorHandler));
}

void AnnotationRegistryHandler::RunPendingCreateCommands() {
  FXL_DCHECK(initialized_);
  for (auto &args : pending_create_args_) {
    annotation_manager_->RequestCreate(id_, std::move(args.main_view),
                                       std::move(args.view_holder_token), std::move(args.callback));
  }
  pending_create_args_.clear();
}

void AnnotationRegistryHandler::InitializeWithGfxAnnotationManager(
    scenic_impl::gfx::AnnotationManager *annotation_manager) {
  FXL_DCHECK(!initialized_);
  FXL_DCHECK(annotation_manager);

  annotation_manager_ = annotation_manager;
  annotation_manager_->RegisterHandler(
      id_, fit::bind_member(this, &AnnotationRegistryHandler::ErrorHandler));

  initialized_ = true;
  RunPendingCreateCommands();
}

// |fuchsia::ui::annotation::Registry|
void AnnotationRegistryHandler::CreateAnnotationViewHolder(
    fuchsia::ui::views::ViewRef main_view, fuchsia::ui::views::ViewHolderToken view_holder_token,
    CreateAnnotationViewHolderCallback callback) {
  if (initialized()) {
    annotation_manager_->RequestCreate(id_, std::move(main_view), std::move(view_holder_token),
                                       std::move(callback));
  } else {
    pending_create_args_.push_back({.main_view = std::move(main_view),
                                    .view_holder_token = std::move(view_holder_token),
                                    .callback = std::move(callback)});
  }
}

}  // namespace scenic_impl
