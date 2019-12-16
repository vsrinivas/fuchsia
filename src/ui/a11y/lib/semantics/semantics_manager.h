// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_MANAGER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_MANAGER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/types.h>

#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"

namespace a11y {

// Factory class to build a new Semantic Tree Service.
class SemanticTreeServiceFactory {
 public:
  SemanticTreeServiceFactory() = default;
  virtual ~SemanticTreeServiceFactory() = default;

  virtual std::unique_ptr<SemanticTreeService> NewService(
      fuchsia::ui::views::ViewRef view_ref,
      fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
      vfs::PseudoDir* debug_dir, SemanticTreeService::CloseChannelCallback close_channel_callback);
};

// A service to manage producing and consuming of semantics.
//
// Semantic Providers connect to this service to start supplying semantic
// information for a particular View while Semantic Consumers query available
// semantic information managed by this service.
class SemanticsManager : public fuchsia::accessibility::semantics::SemanticsManager {
 public:
  explicit SemanticsManager(std::unique_ptr<SemanticTreeServiceFactory> factory,
                            vfs::PseudoDir* debug_dir);
  ~SemanticsManager() override;

  // Function to Enable/Disable Semantics Manager.
  // When Semantics Manager is disabled, all the semantic tree bindings are
  // closed, which deletes all the semantic tree data.
  void SetSemanticsManagerEnabled(bool enabled);

  // Returns a weak pointer to the Semantic Tree owned by the service with
  // |koid| if it exists, nullptr otherwise. Caller must always check if the
  // pointer is valid before accessing, as the pointer may be invalidated. The
  // pointer may become invalidated if the semantic provider disconnects or if
  // an error occurred. This is not thread safe. This pointer may only be used
  // in the same thread as this service is running.
  const fxl::WeakPtr<::a11y::SemanticTree> GetTreeByKoid(const zx_koid_t koid) const;

 private:
  // |fuchsia::accessibility::semantics::SemanticsManager|:
  void RegisterViewForSemantics(
      fuchsia::ui::views::ViewRef view_ref,
      fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override;

  // Closes the service channel of the View with |view_ref_koid| in |semantic_tree_bindings_|.
  void CloseChannel(zx_koid_t view_ref_koid);

  // Helper function to enable semantic updates for all the Views.
  void EnableSemanticsUpdates(bool enabled);

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticTree,
                   std::unique_ptr<SemanticTreeService>>
      semantic_tree_bindings_;

  bool semantics_enabled_ = false;

  std::unique_ptr<SemanticTreeServiceFactory> factory_;

  vfs::PseudoDir* const debug_dir_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_MANAGER_H_
