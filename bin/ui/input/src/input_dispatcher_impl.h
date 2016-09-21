// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_INPUT_MANAGER_INPUT_DISPATCHER_IMPL_H_
#define SERVICES_UI_INPUT_MANAGER_INPUT_DISPATCHER_IMPL_H_

#include <queue>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/services/geometry/interfaces/geometry.mojom.h"
#include "mojo/services/ui/input/interfaces/input_dispatcher.mojom.h"
#include "mojo/services/ui/views/interfaces/view_trees.mojom.h"
#include "mojo/ui/associates/view_tree_hit_tester_client.h"

namespace input_manager {

class InputAssociate;

// InputDispatcher implementation.
// Binds incoming requests to the relevant view token.
class InputDispatcherImpl : public mojo::ui::InputDispatcher {
 public:
  InputDispatcherImpl(
      InputAssociate* associate,
      mojo::ui::ViewTreeTokenPtr view_tree_token,
      mojo::InterfaceRequest<mojo::ui::InputDispatcher> request);
  ~InputDispatcherImpl() override;

  const mojo::ui::ViewTreeToken* view_tree_token() const {
    return view_tree_token_.get();
  }

  // |mojo::ui::InputDispatcher|
  void DispatchEvent(mojo::EventPtr event) override;

 private:
  void ProcessNextEvent();
  void DeliverEvent(mojo::EventPtr event);
  void OnHitTestResult(scoped_ptr<mojo::ui::ResolvedHits> resolved_hits);

  InputAssociate* const associate_;
  mojo::ui::ViewTreeTokenPtr view_tree_token_;
  scoped_refptr<mojo::ui::ViewTreeHitTesterClient> hit_tester_;

  // TODO(jeffbrown): Replace this with a proper pipeline.
  std::queue<mojo::EventPtr> pending_events_;

  // TODO(jeffbrown): This hack is just for scaffolding.  Redesign later.
  mojo::ui::ViewTokenPtr focused_view_token_;
  mojo::TransformPtr focused_view_transform_;

  mojo::Binding<mojo::ui::InputDispatcher> binding_;

  base::WeakPtrFactory<InputDispatcherImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(InputDispatcherImpl);
};

}  // namespace input_manager

#endif  // SERVICES_UI_INPUT_MANAGER_INPUT_DISPATCHER_IMPL_H_
