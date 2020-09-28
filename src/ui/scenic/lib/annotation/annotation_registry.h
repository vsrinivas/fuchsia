// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_ANNOTATION_ANNOTATION_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_ANNOTATION_ANNOTATION_REGISTRY_H_

#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "src/ui/scenic/lib/annotation/annotation_registry_handler.h"
#include "src/ui/scenic/lib/gfx/engine/annotation_manager.h"

namespace scenic_impl {

// AnnotationRegistry registers the fuchsia.ui.annotation.Registry service,
// and manages all the service handlers for each incoming FIDL connection.
//
// TODO(fxbug.dev/45197): This class is thread-compatible and it's safe to use when Scenic
// is single-threaded. We may need to make it thread-safe once Scenic supports
// multithreading.
class AnnotationRegistry {
 public:
  AnnotationRegistry(sys::ComponentContext* component_context,
                     gfx::AnnotationManager* annotation_manager = nullptr);

  // Initialize AnnotationRegistry (and all the annotation handlers) with
  // a gfx AnnotationManager.
  //
  // Currently AnnotationRegistry is created and service is registered when
  // Scenic app starts, while gfx engine is initialized later after Escher
  // is loaded, so we need to do the initialization later.
  void InitializeWithGfxAnnotationManager(gfx::AnnotationManager* annotation_manager);

 private:
  AnnotationHandlerId GetNextHandlerId() { return next_handler_id_++; }

  // Add a new AnnotationRegistryHandler to the |handlers_| map.
  // The |id| should not exist in handlers_.
  void AddHandler(AnnotationHandlerId id, std::unique_ptr<AnnotationRegistryHandler> handler);

  // Remove an existng AnnotationRegistryHandler from the |handlers_| map.
  // The |id| should exist in handlers_.
  void RemoveHandler(AnnotationHandlerId id);

  AnnotationHandlerId next_handler_id_ = 0;

  bool initialized_ = false;
  gfx::AnnotationManager* annotation_manager_ = nullptr;

  std::unordered_map<AnnotationHandlerId, std::unique_ptr<AnnotationRegistryHandler>> handlers_;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_ANNOTATION_ANNOTATION_REGISTRY_H_
