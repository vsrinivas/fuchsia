// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_

#include "apps/mozart/services/composition/hit_tests.fidl.h"
#include "apps/mozart/services/composition/scene_token.fidl.h"
#include "apps/mozart/services/input/ime_service.fidl.h"
#include "apps/mozart/services/input/input_connection.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/mozart/services/views/view_tree_token.fidl.h"
#include "apps/mozart/src/view_manager/internal/resolved_hits.h"

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

// Provides a view associate with the ability to inspect and perform operations
// on the contents of views and view trees.
class ViewInspector {
 public:
  using GetHitTesterCallback = std::function<void(bool)>;
  using ResolveScenesCallback =
      std::function<void(std::vector<mozart::ViewTokenPtr>)>;
  using ResolveFocusChainCallback =
      std::function<void(std::unique_ptr<FocusChain>)>;
  using ActivateFocusChainCallback =
      std::function<void(std::unique_ptr<FocusChain>)>;
  using HasFocusCallback = std::function<void(bool)>;
  using OnEventDelivered = std::function<void(bool handled)>;

  virtual ~ViewInspector() {}

  // Provides an interface which can be used to perform hit tests on the
  // contents of the view tree's scene graph.
  //
  // The |hit_tester| will be closed if the view tree is not attached to a
  // renderer, when it is reattached to a different renderer, or when the
  // view tree is destroyed.
  //
  // The callback will be invoked the hit tester is invalidated.
  // If |renderer_changed| is true, the client should call |GetHitTester|
  // again to obtain a new one.  Otherwise it should assume that the view
  // tree has become unavailable (so no hit tester is available).
  virtual void GetHitTester(
      mozart::ViewTreeTokenPtr view_tree_token,
      fidl::InterfaceRequest<mozart::HitTester> hit_tester_request,
      const GetHitTesterCallback& callback) = 0;

  // Given an array of scene tokens, produces an array of view tokens
  // of equal size containing the view to which the scene belongs or null
  // if the scene token does not belong to any view.
  //
  // It is safe to cache the results of this operation because a scene will
  // only ever be associated with at most one view although a view may
  // create several scenes during its lifetime.
  virtual void ResolveScenes(std::vector<mozart::SceneTokenPtr> scene_tokens,
                             const ResolveScenesCallback& callback) = 0;

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

  // Resolves all of the scene tokens referenced in the hit test result
  // then invokes the callback.
  // Note: May invoke the callback immediately if no remote calls were required.
  virtual void ResolveHits(mozart::HitTestResultPtr hit_test_result,
                           const ResolvedHitsCallback& callback) = 0;
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_INTERNAL_VIEW_INSPECTOR_H_
