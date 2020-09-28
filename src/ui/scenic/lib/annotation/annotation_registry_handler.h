// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_ANNOTATION_ANNOTATION_REGISTRY_HANDLER_H_
#define SRC_UI_SCENIC_LIB_ANNOTATION_ANNOTATION_REGISTRY_HANDLER_H_

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <zircon/status.h>

#include "src/ui/scenic/lib/gfx/engine/annotation_manager.h"

namespace scenic_impl {

using AnnotationHandlerId = uint32_t;

// An implementation of the annotation Registry protocol, which sends the
// CreateAnnotationViewHolder to gfx Annotation Manager once it is initialized.
//
// TODO(fxbug.dev/45197): This class is thread-compatible and it's safe to use when Scenic
// is single-threaded. We may need to make it thread-safe once Scenic supports
// multithreading.
class AnnotationRegistryHandler : fuchsia::ui::annotation::Registry {
 public:
  AnnotationRegistryHandler(fidl::InterfaceRequest<Registry> request,
                            AnnotationHandlerId handler_id,
                            gfx::AnnotationManager* annotation_manager = nullptr);

  bool initialized() const { return initialized_; }

  // Set up the gfx::AnnotationManager and process all pending create commands.
  void InitializeWithGfxAnnotationManager(gfx::AnnotationManager* annotation_manager);

  // Set up error handler. The callback function will be called when the channel
  // is disconnected, and the epitaph will be returned as an argument.
  void SetErrorHandler(fit::function<void(zx_status_t)> error_handler) {
    FX_DCHECK(!error_handler_);
    error_handler_ = std::move(error_handler);
  }

  // |fuchsia::ui::annotation::Registry|.
  //
  // Currently the service is registered when Scenic app starts, while gfx
  // Engine is initialized later after Escher is loaded. All the incoming FIDL
  // requests earlier than that will be deferred until the class is initialized
  // with a gfx::AnnotationManager.
  void CreateAnnotationViewHolder(fuchsia::ui::views::ViewRef main_view,
                                  fuchsia::ui::views::ViewHolderToken view_holder_token,
                                  CreateAnnotationViewHolderCallback callback) override;

 private:
  struct CreateHolderArgs {
    fuchsia::ui::views::ViewRef main_view;
    fuchsia::ui::views::ViewHolderToken view_holder_token;
    CreateAnnotationViewHolderCallback callback;
  };

  // Default error handler. This handles both cases where gfx::AnnotationManager
  // fails, or the client disconnects from the service.
  void ErrorHandler(zx_status_t status) {
    if (binding_.is_bound()) {
      binding_.Close(status);
    }
    if (error_handler_) {
      error_handler_(status);
    }
  }

  // All the handlers are currently identified by IDs in AnnotationRegistry and
  // gfx AnnotationManager. This should be unique across all annotation handlers
  // in one AnnotationRegistry.
  AnnotationHandlerId id_ = 0U;

  fit::function<void(zx_status_t)> error_handler_ = nullptr;

  // These arguments will be used when gfx AnnotationManager is initialized.
  std::vector<CreateHolderArgs> pending_create_args_;
  void RunPendingCreateCommands();

  bool initialized_ = false;
  gfx::AnnotationManager* annotation_manager_ = nullptr;
  fidl::Binding<fuchsia::ui::annotation::Registry> binding_;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_ANNOTATION_ANNOTATION_REGISTRY_HANDLER_H_
