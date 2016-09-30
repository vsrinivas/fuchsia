// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_MANAGER_INPUT_DISPATCHER_IMPL_H_
#define APPS_MOZART_SRC_INPUT_MANAGER_INPUT_DISPATCHER_IMPL_H_

#include <queue>

#include "apps/mozart/lib/view_associate_framework/view_tree_hit_tester_client.h"
#include "apps/mozart/services/input/interfaces/input_dispatcher.mojom.h"
#include "apps/mozart/services/views/interfaces/view_trees.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/services/geometry/interfaces/geometry.mojom.h"

namespace input_manager {

class InputAssociate;

// InputDispatcher implementation.
// Binds incoming requests to the relevant view token.
class InputDispatcherImpl : public mozart::InputDispatcher {
 public:
  InputDispatcherImpl(InputAssociate* associate,
                      mozart::ViewTreeTokenPtr view_tree_token,
                      mojo::InterfaceRequest<mozart::InputDispatcher> request);
  ~InputDispatcherImpl() override;

  const mozart::ViewTreeToken* view_tree_token() const {
    return view_tree_token_.get();
  }

  // |mozart::InputDispatcher|
  void DispatchEvent(mozart::EventPtr event) override;

 private:
  void ProcessNextEvent();
  void DeliverEvent(mozart::EventPtr event);
  void OnHitTestResult(std::unique_ptr<mozart::ResolvedHits> resolved_hits);

  InputAssociate* const associate_;
  mozart::ViewTreeTokenPtr view_tree_token_;
  ftl::RefPtr<mozart::ViewTreeHitTesterClient> hit_tester_;

  // TODO(jeffbrown): Replace this with a proper pipeline.
  std::queue<mozart::EventPtr> pending_events_;

  // TODO(jeffbrown): This hack is just for scaffolding.  Redesign later.
  mozart::ViewTokenPtr focused_view_token_;
  mojo::TransformPtr focused_view_transform_;

  mojo::Binding<mozart::InputDispatcher> binding_;

  ftl::WeakPtrFactory<InputDispatcherImpl> weak_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputDispatcherImpl);
};

}  // namespace input_manager

#endif  // APPS_MOZART_SRC_INPUT_MANAGER_INPUT_DISPATCHER_IMPL_H_
