// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_INPUT_INPUT_DISPATCHER_IMPL_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_INPUT_INPUT_DISPATCHER_IMPL_H_

#include <queue>

#include "apps/mozart/services/geometry/geometry.fidl.h"
#include "apps/mozart/services/input/input_dispatcher.fidl.h"
#include "apps/mozart/services/views/view_trees.fidl.h"
#include "apps/mozart/src/view_manager/internal/view_inspector.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace view_manager {

class ViewInspector;
class InputOwner;

// InputDispatcher implementation.
// Binds incoming requests to the relevant view token.
class InputDispatcherImpl : public mozart::InputDispatcher {
 public:
  InputDispatcherImpl(ViewInspector* inspector,
                      InputOwner* owner,
                      mozart::ViewTreeTokenPtr view_tree_token,
                      fidl::InterfaceRequest<mozart::InputDispatcher> request);
  ~InputDispatcherImpl() override;

  const mozart::ViewTreeToken* view_tree_token() const {
    return view_tree_token_.get();
  }

  // |mozart::InputDispatcher|
  void DispatchEvent(mozart::InputEventPtr event) override;

 private:
  void ProcessNextEvent();
  // Used for located events (touch, stylus)
  void DeliverEvent(mozart::InputEventPtr event);
  void DeliverEvent(uint64_t event_path_propagation_id,
                    size_t index,
                    mozart::InputEventPtr event);
  // Used for key events (keyboard)
  // |propagation_index| is the current index in the |focus_chain|
  void DeliverKeyEvent(std::unique_ptr<FocusChain> focus_chain,
                       uint64_t propagation_index,
                       mozart::InputEventPtr event);
  // Used to post as task and schedule the next call to |DispatchEvent|
  void PopAndScheduleNextEvent();

  void OnFocusResult(std::unique_ptr<FocusChain> focus_chain);
  void OnHitTestResult(const mozart::PointF& point,
                       std::vector<ViewHit> view_hits);

  ViewInspector* const inspector_;
  InputOwner* const owner_;
  mozart::ViewTreeTokenPtr view_tree_token_;

  // TODO(jeffbrown): Replace this with a proper pipeline.
  std::queue<mozart::InputEventPtr> pending_events_;

  std::vector<ViewHit> event_path_;
  uint64_t event_path_propagation_id_ = 0;

  fidl::Binding<mozart::InputDispatcher> binding_;

  std::unique_ptr<FocusChain> active_focus_chain_;

  ftl::WeakPtrFactory<InputDispatcherImpl> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputDispatcherImpl);
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_INPUT_INPUT_DISPATCHER_IMPL_H_
