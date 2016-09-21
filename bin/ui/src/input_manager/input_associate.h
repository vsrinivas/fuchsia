// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_INPUT_MANAGER_INPUT_ASSOCIATE_IMPL_H_
#define SERVICES_UI_INPUT_MANAGER_INPUT_ASSOCIATE_IMPL_H_

#include <memory>
#include <unordered_map>

#include "base/macros.h"
#include "mojo/services/ui/views/interfaces/view_associates.mojom.h"
#include "mojo/ui/associates/view_inspector_client.h"
#include "services/ui/input_manager/input_connection_impl.h"
#include "services/ui/input_manager/input_dispatcher_impl.h"

namespace input_manager {

class ViewTreeHitTester;

// InputManager's ViewAssociate interface implementation.
class InputAssociate : public mojo::ui::ViewAssociate {
 public:
  InputAssociate();
  ~InputAssociate() override;

  const scoped_refptr<mojo::ui::ViewInspectorClient>& inspector() {
    return inspector_;
  }

  // |ViewAssociate|:
  void Connect(mojo::InterfaceHandle<mojo::ui::ViewInspector> inspector,
               const ConnectCallback& callback) override;
  void ConnectToViewService(
      mojo::ui::ViewTokenPtr view_token,
      const mojo::String& service_name,
      mojo::ScopedMessagePipeHandle client_handle) override;
  void ConnectToViewTreeService(
      mojo::ui::ViewTreeTokenPtr view_tree_token,
      const mojo::String& service_name,
      mojo::ScopedMessagePipeHandle client_handle) override;

  // Delivers an event to a view.
  void DeliverEvent(const mojo::ui::ViewToken* view_token,
                    mojo::EventPtr event);

  // Callbacks.
  void OnInputConnectionDied(InputConnectionImpl* connection);
  void OnInputDispatcherDied(InputDispatcherImpl* dispatcher);

 private:
  void CreateInputConnection(
      mojo::ui::ViewTokenPtr view_token,
      mojo::InterfaceRequest<mojo::ui::InputConnection> request);
  void CreateInputDispatcher(
      mojo::ui::ViewTreeTokenPtr view_tree_token,
      mojo::InterfaceRequest<mojo::ui::InputDispatcher> request);

  scoped_refptr<mojo::ui::ViewInspectorClient> inspector_;
  std::unordered_map<uint32_t, std::unique_ptr<InputConnectionImpl>>
      input_connections_by_view_token_;
  std::unordered_map<uint32_t, std::unique_ptr<InputDispatcherImpl>>
      input_dispatchers_by_view_tree_token_;

  DISALLOW_COPY_AND_ASSIGN(InputAssociate);
};

}  // namespace input_manager

#endif  // SERVICES_UI_INPUT_MANAGER_INPUT_ASSOCIATE_IMPL_H_
