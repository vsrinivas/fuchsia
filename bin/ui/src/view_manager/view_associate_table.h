// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_ASSOCIATE_TABLE_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_ASSOCIATE_TABLE_H_

#include <memory>
#include <string>
#include <vector>

#include "apps/mozart/services/views/interfaces/view_associates.mojom.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace view_manager {

// Maintains a table of all connected view associates.
class ViewAssociateTable : public mojo::ui::ViewAssociateOwner {
 public:
  using AssociateConnectionErrorCallback =
      std::function<void(const std::string&)>;

  ViewAssociateTable();
  ~ViewAssociateTable() override;

  void RegisterViewAssociate(
      mojo::ui::ViewInspector* inspector,
      mojo::ui::ViewAssociatePtr associate,
      mojo::InterfaceRequest<mojo::ui::ViewAssociateOwner> view_associate_owner,
      const mojo::String& label);

  void FinishedRegisteringViewAssociates();

  // Connects to services offered by the view associates.
  void ConnectToViewService(mojo::ui::ViewTokenPtr view_token,
                            const mojo::String& service_name,
                            mojo::ScopedMessagePipeHandle client_handle);
  void ConnectToViewTreeService(mojo::ui::ViewTreeTokenPtr view_tree_token,
                                const mojo::String& service_name,
                                mojo::ScopedMessagePipeHandle client_handle);

  void OnConnected(uint32_t index, mojo::ui::ViewAssociateInfoPtr info);

  size_t associate_count();

 private:
  struct AssociateData {
    AssociateData(const std::string& label,
                  mojo::ui::ViewAssociatePtr associate,
                  mojo::ui::ViewAssociateOwner* associate_owner_impl,
                  mojo::ui::ViewInspector* inspector);
    ~AssociateData();

    void BindOwner(mojo::InterfaceRequest<mojo::ui::ViewAssociateOwner>
                       view_associate_owner_request);

    const std::string label;
    mojo::ui::ViewAssociatePtr associate;
    mojo::Binding<mojo::ui::ViewAssociateOwner> associate_owner;
    mojo::ui::ViewAssociateInfoPtr info;
    mojo::Binding<mojo::ui::ViewInspector> inspector_binding;
  };

  bool RemoveAssociateData(AssociateData* associate_data, std::string& label);
  void CompleteDeferredWorkIfReady();
  void OnAssociateOwnerConnectionError(AssociateData* associate_data);
  void OnAssociateConnectionError(AssociateData* associate_data);

  std::vector<std::unique_ptr<AssociateData>> associates_;

  uint32_t pending_connection_count_ = 0u;
  bool waiting_to_register_associates_ = true;
  std::vector<ftl::Closure> deferred_work_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewAssociateTable);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_ASSOCIATE_TABLE_H_
