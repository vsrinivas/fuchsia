// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_

#include <functional>
#include <vector>

#include "apps/mozart/services/geometry/geometry.fidl.h"
#include "apps/mozart/services/input/ime_service.fidl.h"
#include "apps/mozart/services/input/input_connection.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/mozart/services/views/view_tree_token.fidl.h"

namespace view_manager {
class InputConnectionImpl;
class InputDispatcherImpl;

// |FocusChain| defines the chain that a keyboard input event will follow.
struct FocusChain {
 public:
  // |version| of the focus chain.
  uint64_t version;

  // |chain| is the ordered list of views that a keyboard event will propagate
  std::vector<mozart::ViewTokenPtr> chain;
};

// Provides information about a view which was hit during a hit tests.
struct ViewHit {
  // The view which was hit.
  mozart::ViewToken view_token;

  // Transforms the view tree coordinate system to the view's coordinate system.
  mozart::TransformPtr inverse_transform;
};

// Provides a view associate with the ability to inspect and perform operations
// on the contents of views and view trees.
class ViewInspector {
 public:
  using HitTestCallback = std::function<void(std::vector<ViewHit>)>;
  using ResolveFocusChainCallback =
      std::function<void(std::unique_ptr<FocusChain>)>;
  using ActivateFocusChainCallback =
      std::function<void(std::unique_ptr<FocusChain>)>;
  using HasFocusCallback = std::function<void(bool)>;
  using OnEventDelivered = std::function<void(bool handled)>;

  virtual ~ViewInspector() {}

  // Performs a hit test at the given point and returns the list of views
  // which were hit.
  virtual void HitTest(const mozart::ViewTreeToken& view_tree_token,
                       const mozart::PointF& point,
                       HitTestCallback callback) = 0;

  // Given a token for a view tree, retrieve the current active focus chain for
  // this view tree.
  virtual void ResolveFocusChain(mozart::ViewTreeTokenPtr view_tree_token,
                                 const ResolveFocusChainCallback& callback) = 0;

  // TODO(jpoichet) Move this
  // Set the current input focus to the provided |view_token|.
  // This is a back channel from input_manager to view_manager to swap focus
  // on touch down events. This logic should be moved in the future
  virtual void ActivateFocusChain(
      mozart::ViewTokenPtr view_token,
      const ActivateFocusChainCallback& callback) = 0;

  // Returns whether view has focus
  virtual void HasFocus(mozart::ViewTokenPtr view_token,
                        const HasFocusCallback& callback) = 0;

  // Retrieve the SoftKeyboardContainer that is the closest to the ViewToken
  // in the associated ViewTree
  virtual void GetSoftKeyboardContainer(
      mozart::ViewTokenPtr view_token,
      fidl::InterfaceRequest<mozart::SoftKeyboardContainer> container) = 0;

  // Retrieve the IME Service that is the closest to the ViewToken
  // in the associated ViewTree
  virtual void GetImeService(
      mozart::ViewTokenPtr view_token,
      fidl::InterfaceRequest<mozart::ImeService> ime_service) = 0;
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_
