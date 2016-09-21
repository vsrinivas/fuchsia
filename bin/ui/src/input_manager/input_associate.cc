// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/input_manager/input_associate.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "mojo/services/ui/views/cpp/formatting.h"

namespace input_manager {
namespace {
std::ostream& operator<<(std::ostream& os, const mojo::Event& value) {
  os << "{action=" << value.action;
  if (value.pointer_data)
    os << ", x=" << value.pointer_data->x << ", y=" << value.pointer_data->y;
  if (value.key_data)
    os << ", key_code=" << value.key_data->key_code;
  return os << "}";
}
}  // namespace

InputAssociate::InputAssociate() {}

InputAssociate::~InputAssociate() {}

void InputAssociate::Connect(
    mojo::InterfaceHandle<mojo::ui::ViewInspector> inspector,
    const ConnectCallback& callback) {
  DCHECK(inspector);  // checked by mojom

  input_connections_by_view_token_.clear();
  input_dispatchers_by_view_tree_token_.clear();
  inspector_ = new mojo::ui::ViewInspectorClient(
      mojo::ui::ViewInspectorPtr::Create(std::move(inspector)));

  auto info = mojo::ui::ViewAssociateInfo::New();
  info->view_service_names.push_back(mojo::ui::InputConnection::Name_);
  info->view_tree_service_names.push_back(mojo::ui::InputDispatcher::Name_);
  callback.Run(info.Pass());
}

void InputAssociate::ConnectToViewService(
    mojo::ui::ViewTokenPtr view_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  DCHECK(view_token);  // checked by mojom

  if (service_name == mojo::ui::InputConnection::Name_) {
    CreateInputConnection(view_token.Pass(),
                          mojo::InterfaceRequest<mojo::ui::InputConnection>(
                              client_handle.Pass()));
  }
}

void InputAssociate::ConnectToViewTreeService(
    mojo::ui::ViewTreeTokenPtr view_tree_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  DCHECK(view_tree_token);  // checked by mojom

  if (service_name == mojo::ui::InputDispatcher::Name_) {
    CreateInputDispatcher(view_tree_token.Pass(),
                          mojo::InterfaceRequest<mojo::ui::InputDispatcher>(
                              client_handle.Pass()));
  }
}

void InputAssociate::CreateInputConnection(
    mojo::ui::ViewTokenPtr view_token,
    mojo::InterfaceRequest<mojo::ui::InputConnection> request) {
  DCHECK(view_token);
  DCHECK(request.is_pending());
  DVLOG(1) << "CreateInputConnection: view_token=" << view_token;

  const uint32_t view_token_value = view_token->value;
  input_connections_by_view_token_.emplace(
      view_token_value,
      std::unique_ptr<InputConnectionImpl>(
          new InputConnectionImpl(this, view_token.Pass(), request.Pass())));
}

void InputAssociate::OnInputConnectionDied(InputConnectionImpl* connection) {
  DCHECK(connection);
  auto it =
      input_connections_by_view_token_.find(connection->view_token()->value);
  DCHECK(it != input_connections_by_view_token_.end());
  DCHECK(it->second.get() == connection);
  DVLOG(1) << "OnInputConnectionDied: view_token=" << connection->view_token();

  input_connections_by_view_token_.erase(it);
}

void InputAssociate::CreateInputDispatcher(
    mojo::ui::ViewTreeTokenPtr view_tree_token,
    mojo::InterfaceRequest<mojo::ui::InputDispatcher> request) {
  DCHECK(view_tree_token);
  DCHECK(request.is_pending());
  DVLOG(1) << "CreateInputDispatcher: view_tree_token=" << view_tree_token;

  const uint32_t view_tree_token_value = view_tree_token->value;
  input_dispatchers_by_view_tree_token_.emplace(
      view_tree_token_value,
      std::unique_ptr<InputDispatcherImpl>(new InputDispatcherImpl(
          this, view_tree_token.Pass(), request.Pass())));
}

void InputAssociate::OnInputDispatcherDied(InputDispatcherImpl* dispatcher) {
  DCHECK(dispatcher);
  DVLOG(1) << "OnInputDispatcherDied: view_tree_token="
           << dispatcher->view_tree_token();

  auto it = input_dispatchers_by_view_tree_token_.find(
      dispatcher->view_tree_token()->value);
  DCHECK(it != input_dispatchers_by_view_tree_token_.end());
  DCHECK(it->second.get() == dispatcher);

  input_dispatchers_by_view_tree_token_.erase(it);
}

void InputAssociate::DeliverEvent(const mojo::ui::ViewToken* view_token,
                                  mojo::EventPtr event) {
  DCHECK(view_token);
  DCHECK(event);
  DVLOG(1) << "DeliverEvent: view_token=" << *view_token
           << ", event=" << *event;

  auto it = input_connections_by_view_token_.find(view_token->value);
  if (it == input_connections_by_view_token_.end()) {
    DVLOG(1) << "DeliverEvent: dropped because there was no input connection";
    return;
  }

  it->second->DeliverEvent(event.Pass());
}

}  // namespace input_manager
