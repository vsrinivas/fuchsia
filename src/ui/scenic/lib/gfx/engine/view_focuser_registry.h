// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_FOCUSER_REGISTRY_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_FOCUSER_REGISTRY_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include "src/ui/scenic/lib/gfx/id.h"

namespace scenic_impl::gfx {

// An abstract interface for managing fuchsia.ui.views.Focuser lifecycle, starting with FIDL
// requests and ending with cleanup by the owning Session.
class ViewFocuserRegistry {
 public:
  // Forward a FIDL request for fuchsia.ui.views.Focuser, associated with |session_id|.
  // Pre: session_id is unassociated with any fuchsia.ui.views.Focuser.
  virtual void RegisterViewFocuser(
      SessionId session_id, fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) = 0;

  // Notify that a fuchsia.ui.views.Focuser associated with |session_id| can be removed.
  virtual void UnregisterViewFocuser(SessionId session_id) = 0;
};


}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_VIEW_FOCUSER_REGISTRY_H_
