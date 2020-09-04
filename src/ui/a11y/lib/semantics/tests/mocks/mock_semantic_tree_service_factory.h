// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_TREE_SERVICE_FACTORY_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_TREE_SERVICE_FACTORY_H_

#include <zircon/types.h>

#include "src/ui/a11y/lib/semantics/semantic_tree.h"
#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree.h"

namespace accessibility_test {

class MockSemanticTreeServiceFactory : public a11y::SemanticTreeServiceFactory {
 public:
  MockSemanticTreeServiceFactory() = default;
  ~MockSemanticTreeServiceFactory() override = default;

  std::unique_ptr<a11y::SemanticTreeService> NewService(
      zx_koid_t koid, fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
      vfs::PseudoDir* debug_dir,
      a11y::SemanticTreeService::CloseChannelCallback close_channel_callback,
      a11y::SemanticTree::SemanticsEventCallback semantics_event_callback) override;

  a11y::SemanticTreeService* service() { return service_; }
  MockSemanticTree* semantic_tree() { return semantic_tree_ptr_; }

 private:
  a11y::SemanticTreeService* service_ = nullptr;
  std::unique_ptr<MockSemanticTree> semantic_tree_;
  MockSemanticTree* semantic_tree_ptr_ = nullptr;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_TREE_SERVICE_FACTORY_H_
