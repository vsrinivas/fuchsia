// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_associate.h"

#include <utility>

#include "apps/mozart/glue/base/logging.h"
#include "apps/mozart/services/views/cpp/formatting.h"

namespace input_manager {
namespace {
std::ostream& operator<<(std::ostream& os, const mozart::Event& value) {
  os << "{action=" << value.action;
  if (value.pointer_data)
    os << ", x=" << value.pointer_data->x << ", y=" << value.pointer_data->y;
  if (value.key_data)
    os << ", hid_usage=0x" << std::hex << value.key_data->hid_usage;
  return os << "}";
}
}  // namespace

InputAssociate::InputAssociate() {}

InputAssociate::~InputAssociate() {}

void InputAssociate::Connect(
    fidl::InterfaceHandle<mozart::ViewInspector> inspector,
    const ConnectCallback& callback) {
  FTL_DCHECK(inspector);  // checked by fidl

  input_connections_by_view_token_.clear();
  input_dispatchers_by_view_tree_token_.clear();
  inspector_ = ftl::MakeRefCounted<mozart::ViewInspectorClient>(
      mozart::ViewInspectorPtr::Create(std::move(inspector)));

  auto info = mozart::ViewAssociateInfo::New();
  info->view_service_names.push_back(mozart::InputConnection::Name_);
  info->view_tree_service_names.push_back(mozart::InputDispatcher::Name_);
  callback(std::move(info));
}

void InputAssociate::ConnectToViewService(mozart::ViewTokenPtr view_token,
                                          const fidl::String& service_name,
                                          mx::channel client_handle) {
  FTL_DCHECK(view_token);  // checked by fidl

  if (service_name == mozart::InputConnection::Name_) {
    CreateInputConnection(std::move(view_token),
                          fidl::InterfaceRequest<mozart::InputConnection>(
                              std::move(client_handle)));
  }
}

void InputAssociate::ConnectToViewTreeService(
    mozart::ViewTreeTokenPtr view_tree_token,
    const fidl::String& service_name,
    mx::channel client_handle) {
  FTL_DCHECK(view_tree_token);  // checked by fidl

  if (service_name == mozart::InputDispatcher::Name_) {
    CreateInputDispatcher(std::move(view_tree_token),
                          fidl::InterfaceRequest<mozart::InputDispatcher>(
                              std::move(client_handle)));
  }
}

void InputAssociate::CreateInputConnection(
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::InputConnection> request) {
  FTL_DCHECK(view_token);
  FTL_DCHECK(request.is_pending());
  DVLOG(1) << "CreateInputConnection: view_token=" << view_token;

  const uint32_t view_token_value = view_token->value;
  input_connections_by_view_token_.emplace(
      view_token_value,
      std::unique_ptr<InputConnectionImpl>(new InputConnectionImpl(
          this, std::move(view_token), std::move(request))));
}

void InputAssociate::OnInputConnectionDied(InputConnectionImpl* connection) {
  FTL_DCHECK(connection);
  auto it =
      input_connections_by_view_token_.find(connection->view_token()->value);
  FTL_DCHECK(it != input_connections_by_view_token_.end());
  FTL_DCHECK(it->second.get() == connection);
  DVLOG(1) << "OnInputConnectionDied: view_token=" << connection->view_token();

  input_connections_by_view_token_.erase(it);
}

void InputAssociate::CreateInputDispatcher(
    mozart::ViewTreeTokenPtr view_tree_token,
    fidl::InterfaceRequest<mozart::InputDispatcher> request) {
  FTL_DCHECK(view_tree_token);
  FTL_DCHECK(request.is_pending());
  DVLOG(1) << "CreateInputDispatcher: view_tree_token=" << view_tree_token;

  const uint32_t view_tree_token_value = view_tree_token->value;
  input_dispatchers_by_view_tree_token_.emplace(
      view_tree_token_value,
      std::unique_ptr<InputDispatcherImpl>(new InputDispatcherImpl(
          this, std::move(view_tree_token), std::move(request))));
}

void InputAssociate::OnInputDispatcherDied(InputDispatcherImpl* dispatcher) {
  FTL_DCHECK(dispatcher);
  DVLOG(1) << "OnInputDispatcherDied: view_tree_token="
           << dispatcher->view_tree_token();

  auto it = input_dispatchers_by_view_tree_token_.find(
      dispatcher->view_tree_token()->value);
  FTL_DCHECK(it != input_dispatchers_by_view_tree_token_.end());
  FTL_DCHECK(it->second.get() == dispatcher);

  input_dispatchers_by_view_tree_token_.erase(it);
}

void InputAssociate::DeliverEvent(const mozart::ViewToken* view_token,
                                  mozart::EventPtr event) {
  FTL_DCHECK(view_token);
  FTL_DCHECK(event);
  DVLOG(1) << "DeliverEvent: view_token=" << *view_token
           << ", event=" << *event;

  auto it = input_connections_by_view_token_.find(view_token->value);
  if (it == input_connections_by_view_token_.end()) {
    DVLOG(1) << "DeliverEvent: dropped because there was no input connection";
    return;
  }

  it->second->DeliverEvent(std::move(event));
}

}  // namespace input_manager
