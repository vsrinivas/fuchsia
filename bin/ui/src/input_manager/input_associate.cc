// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/input_manager/input_associate.h"

#include <utility>

#include "apps/mozart/services/input/cpp/formatting.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/functional/make_copyable.h"

namespace input_manager {

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
  info->view_service_names.push_back(mozart::TextInputService::Name_);
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
  } else if (service_name == mozart::TextInputService::Name_) {
    CreateTextInputService(std::move(view_token),
                           fidl::InterfaceRequest<mozart::TextInputService>(
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
  FTL_VLOG(1) << "CreateInputConnection: view_token=" << view_token;

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
  FTL_VLOG(1) << "OnInputConnectionDied: view_token="
              << connection->view_token();

  input_connections_by_view_token_.erase(it);
}

void InputAssociate::CreateTextInputService(
    mozart::ViewTokenPtr view_token,
    fidl::InterfaceRequest<mozart::TextInputService> request) {
  FTL_DCHECK(view_token);
  FTL_DCHECK(request.is_pending());
  FTL_VLOG(1) << "CreateTextInputService: view_token=" << view_token;

  // TODO monitor focus of this view to disconnect when lost
  auto token = view_token.Clone();
  inspector_->view_inspector()->HasFocus(
      std::move(view_token), ftl::MakeCopyable([
        this, token = std::move(token), request = std::move(request)
      ](bool focused) mutable {
        if (focused) {
          const uint32_t view_token_value = token->value;
          text_input_services_by_view_token_.emplace(
              view_token_value,
              std::unique_ptr<TextInputServiceImpl>(new TextInputServiceImpl(
                  this, std::move(token), std::move(request))));
        }
      }));
}

void InputAssociate::OnTextInputServiceDied(
    TextInputServiceImpl* text_input_service) {
  FTL_DCHECK(text_input_service);
  auto it = text_input_services_by_view_token_.find(
      text_input_service->view_token()->value);

  FTL_DCHECK(it != text_input_services_by_view_token_.end());
  FTL_DCHECK(it->second.get() == text_input_service);
  FTL_VLOG(1) << "OnTextInputServiceDied: view_token="
              << text_input_service->view_token();

  text_input_services_by_view_token_.erase(it);
}

void InputAssociate::CreateInputDispatcher(
    mozart::ViewTreeTokenPtr view_tree_token,
    fidl::InterfaceRequest<mozart::InputDispatcher> request) {
  FTL_DCHECK(view_tree_token);
  FTL_DCHECK(request.is_pending());
  FTL_VLOG(1) << "CreateInputDispatcher: view_tree_token=" << view_tree_token;

  const uint32_t view_tree_token_value = view_tree_token->value;
  input_dispatchers_by_view_tree_token_.emplace(
      view_tree_token_value,
      std::unique_ptr<InputDispatcherImpl>(new InputDispatcherImpl(
          this, std::move(view_tree_token), std::move(request))));
}

void InputAssociate::OnInputDispatcherDied(InputDispatcherImpl* dispatcher) {
  FTL_DCHECK(dispatcher);
  FTL_VLOG(1) << "OnInputDispatcherDied: view_tree_token="
              << dispatcher->view_tree_token();

  auto it = input_dispatchers_by_view_tree_token_.find(
      dispatcher->view_tree_token()->value);
  FTL_DCHECK(it != input_dispatchers_by_view_tree_token_.end());
  FTL_DCHECK(it->second.get() == dispatcher);

  input_dispatchers_by_view_tree_token_.erase(it);
}

void InputAssociate::DeliverEvent(const mozart::ViewToken* view_token,
                                  mozart::InputEventPtr event,
                                  OnEventDelivered callback) {
  FTL_DCHECK(view_token);
  FTL_DCHECK(event);
  FTL_VLOG(1) << "DeliverEvent: view_token=" << *view_token
              << ", event=" << *event;

  auto it = input_connections_by_view_token_.find(view_token->value);
  if (it == input_connections_by_view_token_.end()) {
    FTL_VLOG(1)
        << "DeliverEvent: dropped because there was no input connection";
    return;
  }

  it->second->DeliverEvent(std::move(event), [callback](bool handled) {
    if (callback)
      callback(handled);
  });
}

void InputAssociate::ViewHitTest(
    const mozart::ViewToken* view_token,
    mozart::PointFPtr point,
    const mozart::ViewHitTester::HitTestCallback& callback) {
  FTL_DCHECK(view_token);
  FTL_DCHECK(point);
  FTL_VLOG(1) << "ViewHitTest: view_token=" << *view_token
              << ", event=" << *point;

  auto it = input_connections_by_view_token_.find(view_token->value);
  if (it == input_connections_by_view_token_.end()) {
    FTL_VLOG(1) << "ViewHitTest: dropped because there was no input connection "
                << *view_token;
    callback(true, nullptr);
    return;
  }

  it->second->HitTest(std::move(point), callback);
}

}  // namespace input_manager
