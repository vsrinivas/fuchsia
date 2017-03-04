// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_
#define APPS_MOZART_SRC_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_

#include "apps/mozart/services/input/input_connection.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace input_manager {

class InputAssociate;

using OnEventDelivered = std::function<void(bool handled)>;

// InputConnection implementation.
// Binds incoming requests to the relevant view token.
class InputConnectionImpl : public mozart::InputConnection {
 public:
  InputConnectionImpl(InputAssociate* associate,
                      mozart::ViewTokenPtr view_token,
                      fidl::InterfaceRequest<mozart::InputConnection> request);
  ~InputConnectionImpl() override;

  const mozart::ViewToken* view_token() const { return view_token_.get(); }

  // Delivers an event to a view.
  void DeliverEvent(mozart::InputEventPtr event, OnEventDelivered callback);
  // Hit test a view.
  void HitTest(mozart::PointFPtr point,
               const mozart::ViewHitTester::HitTestCallback& callback);

  // |mozart::InputConnection|
  void SetEventListener(
      fidl::InterfaceHandle<mozart::InputListener> listener) override;
  void SetViewHitTester(
      fidl::InterfaceHandle<mozart::ViewHitTester> listener) override;

 private:
  InputAssociate* const associate_;
  mozart::ViewTokenPtr view_token_;
  mozart::InputListenerPtr event_listener_;
  mozart::ViewHitTesterPtr view_hit_listener_;

  fidl::Binding<mozart::InputConnection> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputConnectionImpl);
};

}  // namespace input_manager

#endif  // APPS_MOZART_SRC_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_
