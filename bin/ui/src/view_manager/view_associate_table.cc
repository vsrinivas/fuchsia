// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_associate_table.h"

#include <algorithm>

#include "apps/mozart/services/views/cpp/formatting.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace view_manager {

template <typename T>
static bool Contains(const fidl::Array<T>& array, const T& value) {
  return std::find(array.storage().cbegin(), array.storage().cend(), value) !=
         array.storage().cend();
}

ViewAssociateTable::ViewAssociateTable() {}

ViewAssociateTable::~ViewAssociateTable() {}

void ViewAssociateTable::RegisterViewAssociate(
    mozart::ViewInspector* inspector,
    mozart::ViewAssociatePtr associate,
    fidl::InterfaceRequest<mozart::ViewAssociateOwner>
        view_associate_owner_request,
    const fidl::String& label) {
  FTL_DCHECK(inspector);
  FTL_DCHECK(associate.is_bound());

  std::string sanitized_label =
      label.get().substr(0, mozart::ViewManager::kLabelMaxLength);
  associates_.emplace_back(new AssociateData(
      sanitized_label, std::move(associate), this, inspector));
  AssociateData* data = associates_.back().get();

  data->BindOwner(std::move(view_associate_owner_request));

  // Set it to use our error handler.
  data->associate.set_connection_error_handler(
      [this, data] { OnAssociateConnectionError(data); });

  data->associate_owner.set_connection_error_handler(
      [this, data] { OnAssociateOwnerConnectionError(data); });

  // Connect the associate to our view inspector.
  mozart::ViewInspectorPtr inspector_ptr;
  data->inspector_binding.Bind(inspector_ptr.NewRequest());
  data->associate->Connect(std::move(inspector_ptr), [
    this, index = pending_connection_count_
  ](mozart::ViewAssociateInfoPtr info) {
    OnConnected(index, std::move(info));
  });

  // Wait for the associate to connect to our view inspector.
  pending_connection_count_++;
}

void ViewAssociateTable::FinishedRegisteringViewAssociates() {
  waiting_to_register_associates_ = false;

  // If no more pending connections, kick off deferred work
  CompleteDeferredWorkIfReady();
}

void ViewAssociateTable::ConnectToViewService(mozart::ViewTokenPtr view_token,
                                              const fidl::String& service_name,
                                              mx::channel client_handle) {
  if (waiting_to_register_associates_ || pending_connection_count_) {
    deferred_work_.push_back(ftl::MakeCopyable([
      this, token = std::move(view_token), service_name,
      handle = std::move(client_handle)
    ]() mutable {
      ConnectToViewService(std::move(token), service_name, std::move(handle));
    }));
    return;
  }

  for (auto& data : associates_) {
    FTL_DCHECK(data->info);
    if (Contains(data->info->view_service_names, service_name)) {
      FTL_VLOG(2) << "Connecting to view service: view_token=" << view_token
                  << ", service_name=" << service_name
                  << ", associate_label=" << data->label;
      FTL_DCHECK(data->associate);
      data->associate->ConnectToViewService(std::move(view_token), service_name,
                                            std::move(client_handle));
      return;
    }
  }

  FTL_VLOG(2) << "Requested view service not available: view_token="
              << view_token << ", service_name=" << service_name;
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
  FTL_VLOG(2) << "ViewAssociate disconnected, removing from table"
              << ", associate_label=" << label;
}

void ViewAssociateTable::OnAssociateOwnerConnectionError(
    AssociateData* associate_data) {
  std::string label;
  bool removed = RemoveAssociateData(associate_data, label);
  FTL_DCHECK(removed);
  FTL_VLOG(2) << "ViewAssociateOwner disconnected, removing from table"
              << ", associate_label=" << label;
}

void ViewAssociateTable::ConnectToViewTreeService(
    mozart::ViewTreeTokenPtr view_tree_token,
    const fidl::String& service_name,
    mx::channel client_handle) {
  if (waiting_to_register_associates_ || pending_connection_count_) {
    deferred_work_.push_back(ftl::MakeCopyable([
      this, token = std::move(view_tree_token), service_name,
      handle = std::move(client_handle)
    ]() mutable {
      ConnectToViewTreeService(std::move(token), service_name,
                               std::move(handle));
    }));
    return;
  }

  for (auto& data : associates_) {
    FTL_DCHECK(data->info);
    if (Contains(data->info->view_tree_service_names, service_name)) {
      FTL_VLOG(2) << "Connecting to view tree service: view_tree_token="
                  << view_tree_token << ", service_name=" << service_name
                  << ", associate_label=" << data->label;
      FTL_DCHECK(data->associate);
      data->associate->ConnectToViewTreeService(
          std::move(view_tree_token), service_name, std::move(client_handle));
      return;
    }
  }

  FTL_VLOG(2) << "Requested view tree service not available: view_tree_token="
              << view_tree_token << ", service_name=" << service_name;
  // Allow pipe to be closed as an indication of failure.
}

void ViewAssociateTable::OnConnected(uint32_t index,
                                     mozart::ViewAssociateInfoPtr info) {
  FTL_DCHECK(info);
  FTL_DCHECK(pending_connection_count_);
  FTL_DCHECK(!associates_[index]->info);

  FTL_VLOG(1) << "Connected to view associate: label="
              << associates_[index]->label << ", info=" << info;
  associates_[index]->info = std::move(info);

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
      associate(std::move(associate)),
      associate_owner(associate_owner_impl),
      inspector_binding(inspector) {}

ViewAssociateTable::AssociateData::~AssociateData() {}

void ViewAssociateTable::AssociateData::BindOwner(
    fidl::InterfaceRequest<mozart::ViewAssociateOwner>
        view_associate_owner_request) {
  associate_owner.Bind(std::move(view_associate_owner_request));
}

}  // namespace view_manager
