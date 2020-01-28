// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/annotation/annotation_registry.h"

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <zircon/status.h>

#include "src/ui/scenic/lib/annotation/annotation_registry_handler.h"
#include "src/ui/scenic/lib/gfx/engine/annotation_manager.h"

namespace scenic_impl {

AnnotationRegistry::AnnotationRegistry(sys::ComponentContext* component_context,
                                       gfx::AnnotationManager* annotation_manager) {
  if (annotation_manager) {
    InitializeWithGfxAnnotationManager(annotation_manager);
  }

  fidl::InterfaceRequestHandler<fuchsia::ui::annotation::Registry> request_handler =
      [this](fidl::InterfaceRequest<fuchsia::ui::annotation::Registry> request) {
        auto handler_id = GetNextHandlerId();
        auto handler = std::make_unique<AnnotationRegistryHandler>(std::move(request), handler_id,
                                                                   annotation_manager_);
        AddHandler(handler_id, std::move(handler));
        handlers_[handler_id]->SetErrorHandler([this, handler_id](zx_status_t status) {
          FXL_LOG(ERROR) << "AnnotationRegistryHandler disconnected. EPITAPH = "
                         << zx_status_get_string(status);
          RemoveHandler(handler_id);
        });
      };

  auto status = component_context->outgoing()->AddPublicService(
      std::move(request_handler), fuchsia::ui::annotation::Registry::Name_);
  FXL_DCHECK(status == ZX_OK);
}

void AnnotationRegistry::InitializeWithGfxAnnotationManager(
    gfx::AnnotationManager* annotation_manager) {
  FXL_DCHECK(!initialized_) << "AnnotationRegistry is already initialized";
  FXL_DCHECK(annotation_manager);

  annotation_manager_ = annotation_manager;
  initialized_ = true;
  for (const auto& kv : handlers_) {
    kv.second->InitializeWithGfxAnnotationManager(annotation_manager_);
  }
}

void AnnotationRegistry::AddHandler(AnnotationHandlerId id,
                                    std::unique_ptr<AnnotationRegistryHandler> handler) {
  FXL_DCHECK(handlers_.find(id) == handlers_.end()) << "Handler with ID = " << id << " exists!";
  handlers_[id] = std::move(handler);
}

void AnnotationRegistry::RemoveHandler(AnnotationHandlerId id) {
  FXL_DCHECK(handlers_.find(id) != handlers_.end())
      << "Handler with ID = " << id << " doesn't exist!";
  handlers_.erase(id);
}

}  // namespace scenic_impl
