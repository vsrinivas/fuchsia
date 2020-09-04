// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree_service_factory.h"

namespace accessibility_test {

std::unique_ptr<a11y::SemanticTreeService> MockSemanticTreeServiceFactory::NewService(
    zx_koid_t koid, fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
    vfs::PseudoDir* debug_dir,
    a11y::SemanticTreeService::CloseChannelCallback close_channel_callback,
    a11y::SemanticTree::SemanticsEventCallback semantics_event_callback) {
  semantic_tree_ = std::make_unique<MockSemanticTree>();
  semantic_tree_ptr_ = semantic_tree_.get();
  auto service = std::make_unique<a11y::SemanticTreeService>(
      std::move(semantic_tree_), koid, std::move(semantic_listener), debug_dir,
      std::move(close_channel_callback));
  service_ = service.get();
  return service;
}

}  // namespace accessibility_test
