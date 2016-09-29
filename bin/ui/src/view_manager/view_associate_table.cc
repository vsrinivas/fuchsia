// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_associate_table.h"

#include <algorithm>

#include "apps/mozart/glue/base/logging.h"
#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/application/connect.h"

namespace view_manager {

template <typename T>
static bool Contains(const mojo::Array<T>& array, const T& value) {
  return std::find(array.storage().cbegin(), array.storage().cend(), value) !=
         array.storage().cend();
}

ViewAssociateTable::ViewAssociateTable() {}

ViewAssociateTable::~ViewAssociateTable() {}

void ViewAssociateTable::RegisterViewAssociate(
    mozart::ViewInspector* inspector,
    mozart::ViewAssociatePtr associate,
    mojo::InterfaceRequest<mozart::ViewAssociateOwner>
        view_associate_owner_request,
    const mojo::String& label) {
  FTL_DCHECK(inspector);
  FTL_DCHECK(associate.is_bound());

  std::string sanitized_label =
      label.get().substr(0, mozart::ViewManager::kLabelMaxLength);
  associates_.emplace_back(
      new AssociateData(sanitized_label, associate.Pass(), this, inspector));
  AssociateData* data = associates_.back().get();

  data->BindOwner(view_associate_owner_request.Pass());

  // Set it to use our error handler.
  data->associate.set_connection_error_handler(
      [this, data] { OnAssociateConnectionError(data); });

  data->associate_owner.set_connection_error_handler(
      [this, data] { OnAssociateOwnerConnectionError(data); });

  // Connect the associate to our view inspector.
  mozart::ViewInspectorPtr inspector_ptr;
  data->inspector_binding.Bind(GetProxy(&inspector_ptr));
  data->associate->Connect(inspector_ptr.Pass(), [
    this, index = pending_connection_count_
  ](mozart::ViewAssociateInfoPtr info) { OnConnected(index, info.Pass()); });

  // Wait for the associate to connect to our view inspector.
  pending_connection_count_++;
}

void ViewAssociateTable::FinishedRegisteringViewAssociates() {
  waiting_to_register_associates_ = false;

  // If no more pending connections, kick off deferred work
  CompleteDeferredWorkIfReady();
}

void ViewAssociateTable::ConnectToViewService(
    mozart::ViewTokenPtr view_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  if (waiting_to_register_associates_ || pending_connection_count_) {
    deferred_work_.push_back(ftl::MakeCopyable([
      this, token = view_token.Pass(), service_name,
      handle = client_handle.Pass()
    ]() mutable {
      ConnectToViewService(std::move(token), service_name, std::move(handle));
    }));
    return;
  }

  for (auto& data : associates_) {
    FTL_DCHECK(data->info);
    if (Contains(data->info->view_service_names, service_name)) {
      DVLOG(2) << "Connecting to view service: view_token=" << view_token
               << ", service_name=" << service_name
               << ", associate_label=" << data->label;
      FTL_DCHECK(data->associate);
      data->associate->ConnectToViewService(view_token.Pass(), service_name,
                                            client_handle.Pass());
      return;
    }
  }

  DVLOG(2) << "Requested view service not available: view_token=" << view_token
           << ", service_name=" << service_name;
  // Allow pipe to be closed as an indication of failure.
}

bool ViewAssociateTable::RemoveAssociateData(AssociateData* associate_data,
                                             std::string& label) {
  for (auto it = associates_.begin(); it != associates_.end(); it++) {
    AssociateData* data = it->get();
    if (associate_data == data) {
      label = data->label;
      associates_.erase(it);
      return true;
    }
  }
  return false;
}

void ViewAssociateTable::OnAssociateConnectionError(
    AssociateData* associate_data) {
  std::string label;
  bool removed = RemoveAssociateData(associate_data, label);
  FTL_DCHECK(removed);
  DVLOG(2) << "ViewAssociate disconnected, removing from table"
           << ", associate_label=" << label;
}

void ViewAssociateTable::OnAssociateOwnerConnectionError(
    AssociateData* associate_data) {
  std::string label;
  bool removed = RemoveAssociateData(associate_data, label);
  FTL_DCHECK(removed);
  DVLOG(2) << "ViewAssociateOwner disconnected, removing from table"
           << ", associate_label=" << label;
}

void ViewAssociateTable::ConnectToViewTreeService(
    mozart::ViewTreeTokenPtr view_tree_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  if (waiting_to_register_associates_ || pending_connection_count_) {
    deferred_work_.push_back(ftl::MakeCopyable([
      this, token = view_tree_token.Pass(), service_name,
      handle = client_handle.Pass()
    ]() mutable {
      ConnectToViewTreeService(std::move(token), service_name,
                               std::move(handle));
    }));
    return;
  }

  for (auto& data : associates_) {
    FTL_DCHECK(data->info);
    if (Contains(data->info->view_tree_service_names, service_name)) {
      DVLOG(2) << "Connecting to view tree service: view_tree_token="
               << view_tree_token << ", service_name=" << service_name
               << ", associate_label=" << data->label;
      FTL_DCHECK(data->associate);
      data->associate->ConnectToViewTreeService(
          view_tree_token.Pass(), service_name, client_handle.Pass());
      return;
    }
  }

  DVLOG(2) << "Requested view tree service not available: view_tree_token="
           << view_tree_token << ", service_name=" << service_name;
  // Allow pipe to be closed as an indication of failure.
}

void ViewAssociateTable::OnConnected(uint32_t index,
                                     mozart::ViewAssociateInfoPtr info) {
  FTL_DCHECK(info);
  FTL_DCHECK(pending_connection_count_);
  FTL_DCHECK(!associates_[index]->info);

  DVLOG(1) << "Connected to view associate: label=" << associates_[index]->label
           << ", info=" << info;
  associates_[index]->info = info.Pass();

  pending_connection_count_--;
  CompleteDeferredWorkIfReady();
}

void ViewAssociateTable::CompleteDeferredWorkIfReady() {
  // We check to see if all the ViewAssociates have been registered, and if
  // they connected to us. Otherwise, we keep the work deferred.
  if (!waiting_to_register_associates_ && !pending_connection_count_) {
    for (auto& work : deferred_work_)
      work();
    deferred_work_.clear();
  }
}

size_t ViewAssociateTable::associate_count() {
  return associates_.size();
}

ViewAssociateTable::AssociateData::AssociateData(
    const std::string& label,
    mozart::ViewAssociatePtr associate,
    mozart::ViewAssociateOwner* associate_owner_impl,
    mozart::ViewInspector* inspector)
    : label(label),
      associate(associate.Pass()),
      associate_owner(associate_owner_impl),
      inspector_binding(inspector) {}

ViewAssociateTable::AssociateData::~AssociateData() {}

void ViewAssociateTable::AssociateData::BindOwner(
    mojo::InterfaceRequest<mozart::ViewAssociateOwner>
        view_associate_owner_request) {
  associate_owner.Bind(view_associate_owner_request.Pass());
}

}  // namespace view_manager
