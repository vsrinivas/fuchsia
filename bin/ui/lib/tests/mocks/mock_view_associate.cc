// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/tests/mocks/mock_view_associate.h"

namespace mozart {
namespace test {

MockViewAssociate::MockViewAssociate() {}
MockViewAssociate::~MockViewAssociate() {}

void MockViewAssociate::Connect(
    fidl::InterfaceHandle<mozart::ViewInspector> inspector,
    const ConnectCallback& callback) {
  connect_invokecount++;

  auto info = mozart::ViewAssociateInfo::New();
  callback(std::move(info));
}

void MockViewAssociate::ConnectToViewService(mozart::ViewTokenPtr view_token,
                                             const fidl::String& service_name,
                                             mx::channel client_handle) {}

void MockViewAssociate::ConnectToViewTreeService(
    mozart::ViewTreeTokenPtr view_tree_token,
    const fidl::String& service_name,
    mx::channel client_handle) {}

}  // namespace test
}  // namespace mozart
