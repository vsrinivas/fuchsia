// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/view_manager/view_associate_table.h"

#include <algorithm>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/services/ui/views/cpp/formatting.h"

namespace view_manager {

template <typename T>
static bool Contains(const mojo::Array<T>& array, const T& value) {
  return std::find(array.storage().cbegin(), array.storage().cend(), value) !=
         array.storage().cend();
}

ViewAssociateTable::ViewAssociateTable() {}

ViewAssociateTable::~ViewAssociateTable() {}

void ViewAssociateTable::RegisterViewAssociate(
    mojo::ui::ViewInspector* inspector,
    mojo::ui::ViewAssociatePtr associate,
    mojo::InterfaceRequest<mojo::ui::ViewAssociateOwner>
        view_associate_owner_request,
    const mojo::String& label) {
  DCHECK(inspector);
  DCHECK(associate.is_bound());

  std::string sanitized_label =
      label.get().substr(0, mojo::ui::kLabelMaxLength);
  associates_.emplace_back(
      new AssociateData(sanitized_label, associate.Pass(), this, inspector));
  AssociateData* data = associates_.back().get();

  data->BindOwner(view_associate_owner_request.Pass());

  // Set it to use our error handler.
  data->associate.set_connection_error_handler(
      base::Bind(&ViewAssociateTable::OnAssociateConnectionError,
                 base::Unretained(this), data));

  data->associate_owner.set_connection_error_handler(
      base::Bind(&ViewAssociateTable::OnAssociateOwnerConnectionError,
                 base::Unretained(this), data));

  // Connect the associate to our view inspector.
  mojo::ui::ViewInspectorPtr inspector_ptr;
  data->inspector_binding.Bind(GetProxy(&inspector_ptr));
  data->associate->Connect(
      inspector_ptr.Pass(),
      base::Bind(&ViewAssociateTable::OnConnected, base::Unretained(this),
                 pending_connection_count_));

  // Wait for the associate to connect to our view inspector.
  pending_connection_count_++;
}

void ViewAssociateTable::FinishedRegisteringViewAssociates() {
  waiting_to_register_associates_ = false;

  // If no more pending connections, kick off deferred work
  CompleteDeferredWorkIfReady();
}

void ViewAssociateTable::ConnectToViewService(
    mojo::ui::ViewTokenPtr view_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  if (waiting_to_register_associates_ || pending_connection_count_) {
    deferred_work_.push_back(
        base::Bind(&ViewAssociateTable::ConnectToViewService,
                   base::Unretained(this), base::Passed(view_token.Pass()),
                   service_name, base::Passed(client_handle.Pass())));
    return;
  }

  for (auto& data : associates_) {
    DCHECK(data->info);
    if (Contains(data->info->view_service_names, service_name)) {
      DVLOG(2) << "Connecting to view service: view_token=" << view_token
               << ", service_name=" << service_name
               << ", associate_label=" << data->label;
      DCHECK(data->associate);
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
  DCHECK(removed);
  DVLOG(2) << "ViewAssociate disconnected, removing from table"
           << ", associate_label=" << label;
}

void ViewAssociateTable::OnAssociateOwnerConnectionError(
    AssociateData* associate_data) {
  std::string label;
  bool removed = RemoveAssociateData(associate_data, label);
  DCHECK(removed);
  DVLOG(2) << "ViewAssociateOwner disconnected, removing from table"
           << ", associate_label=" << label;
}

void ViewAssociateTable::ConnectToViewTreeService(
    mojo::ui::ViewTreeTokenPtr view_tree_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  if (waiting_to_register_associates_ || pending_connection_count_) {
    deferred_work_.push_back(
        base::Bind(&ViewAssociateTable::ConnectToViewTreeService,
                   base::Unretained(this), base::Passed(view_tree_token.Pass()),
                   service_name, base::Passed(client_handle.Pass())));
    return;
  }

  for (auto& data : associates_) {
    DCHECK(data->info);
    if (Contains(data->info->view_tree_service_names, service_name)) {
      DVLOG(2) << "Connecting to view tree service: view_tree_token="
               << view_tree_token << ", service_name=" << service_name
               << ", associate_label=" << data->label;
      DCHECK(data->associate);
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
                                     mojo::ui::ViewAssociateInfoPtr info) {
  DCHECK(info);
  DCHECK(pending_connection_count_);
  DCHECK(!associates_[index]->info);

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
      work.Run();
    deferred_work_.clear();
  }
}

size_t ViewAssociateTable::associate_count() {
  return associates_.size();
}

ViewAssociateTable::AssociateData::AssociateData(
    const std::string& label,
    mojo::ui::ViewAssociatePtr associate,
    mojo::ui::ViewAssociateOwner* associate_owner_impl,
    mojo::ui::ViewInspector* inspector)
    : label(label),
      associate(associate.Pass()),
      associate_owner(associate_owner_impl),
      inspector_binding(inspector) {}

ViewAssociateTable::AssociateData::~AssociateData() {}

void ViewAssociateTable::AssociateData::BindOwner(
    mojo::InterfaceRequest<mojo::ui::ViewAssociateOwner>
        view_associate_owner_request) {
  associate_owner.Bind(view_associate_owner_request.Pass());
}

}  // namespace view_manager
