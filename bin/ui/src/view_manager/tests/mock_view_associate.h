// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/services/views/interfaces/views.mojom.h"

namespace view_manager {
namespace test {

class MockViewAssociate : public mojo::ui::ViewAssociate {
 public:
  MockViewAssociate();
  ~MockViewAssociate() override;

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

  int connect_invokecount = 0;
};

}  // namespace test
}  // namespace view_manager
