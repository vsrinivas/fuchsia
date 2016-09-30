// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCK_VIEW_ASSOCIATE_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCK_VIEW_ASSOCIATE_H_

#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/services/views/interfaces/views.mojom.h"

namespace view_manager {
namespace test {

class MockViewAssociate : public mozart::ViewAssociate {
 public:
  MockViewAssociate();
  ~MockViewAssociate() override;

  void Connect(mojo::InterfaceHandle<mozart::ViewInspector> inspector,
               const ConnectCallback& callback) override;

  void ConnectToViewService(
      mozart::ViewTokenPtr view_token,
      const mojo::String& service_name,
      mojo::ScopedMessagePipeHandle client_handle) override;

  void ConnectToViewTreeService(
      mozart::ViewTreeTokenPtr view_tree_token,
      const mojo::String& service_name,
      mojo::ScopedMessagePipeHandle client_handle) override;

  int connect_invokecount = 0;
};

}  // namespace test
}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCK_VIEW_ASSOCIATE_H_
