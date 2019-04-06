// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

#include "semantics_manager_impl.h"

namespace a11y_manager {

void SemanticsManagerImpl::SetDebugDirectory(vfs::PseudoDir* debug_dir) {
  debug_dir_ = debug_dir;
}

void SemanticsManagerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticsManager>
        request) {
  bindings_.AddBinding(this, std::move(request));
}

void SemanticsManagerImpl::RegisterView(
    zx::event view_ref,
    fidl::InterfaceHandle<
        fuchsia::accessibility::semantics::SemanticActionListener>
        handle,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
        semantic_tree_request) {
  fuchsia::accessibility::semantics::SemanticActionListenerPtr action_listener =
      handle.Bind();
  // TODO(MI4-1736): Log View information in below error handler, once ViewRef
  // support is added.
  action_listener.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Semantic Provider disconnected with status: "
                   << zx_status_get_string(status);
  });

  auto semantic_tree_impl = std::make_unique<SemanticTreeImpl>(
      std::move(view_ref), std::move(action_listener), debug_dir_);

  semantic_tree_bindings_.AddBinding(std::move(semantic_tree_impl),
                                     std::move(semantic_tree_request));
}

}  // namespace a11y_manager
