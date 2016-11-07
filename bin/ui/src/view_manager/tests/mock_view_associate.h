// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCK_VIEW_ASSOCIATE_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCK_VIEW_ASSOCIATE_H_

#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/views.fidl.h"

namespace view_manager {
namespace test {

class MockViewAssociate : public mozart::ViewAssociate {
 public:
  MockViewAssociate();
  ~MockViewAssociate() override;

  void Connect(fidl::InterfaceHandle<mozart::ViewInspector> inspector,
               const ConnectCallback& callback) override;

  void ConnectToViewService(mozart::ViewTokenPtr view_token,
                            const fidl::String& service_name,
                            mx::channel client_handle) override;

  void ConnectToViewTreeService(mozart::ViewTreeTokenPtr view_tree_token,
                                const fidl::String& service_name,
                                mx::channel client_handle) override;

  int connect_invokecount = 0;
};

}  // namespace test
}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_TESTS_MOCK_VIEW_ASSOCIATE_H_
