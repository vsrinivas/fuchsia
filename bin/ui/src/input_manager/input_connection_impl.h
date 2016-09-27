// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_
#define SERVICES_UI_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_

#include "apps/mozart/services/input/interfaces/input_connection.mojom.h"
#include "apps/mozart/services/views/interfaces/views.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"

namespace input_manager {

class InputAssociate;

// InputConnection implementation.
// Binds incoming requests to the relevant view token.
class InputConnectionImpl : public mojo::ui::InputConnection {
 public:
  InputConnectionImpl(
      InputAssociate* associate,
      mojo::ui::ViewTokenPtr view_token,
      mojo::InterfaceRequest<mojo::ui::InputConnection> request);
  ~InputConnectionImpl() override;

  const mojo::ui::ViewToken* view_token() const { return view_token_.get(); }

  // Delivers an event to a view.
  void DeliverEvent(mojo::EventPtr event);

  // |mojo::ui::InputConnection|
  void SetListener(
      mojo::InterfaceHandle<mojo::ui::InputListener> listener) override;

 private:
  void OnEventFinished(bool handled);

  InputAssociate* const associate_;
  mojo::ui::ViewTokenPtr view_token_;
  mojo::ui::InputListenerPtr listener_;

  mojo::Binding<mojo::ui::InputConnection> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputConnectionImpl);
};

}  // namespace input_manager

#endif  // SERVICES_UI_INPUT_MANAGER_INPUT_CONNECTION_IMPL_H_
