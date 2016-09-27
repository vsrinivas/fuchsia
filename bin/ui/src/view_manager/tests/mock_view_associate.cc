// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/tests/mock_view_associate.h"

namespace view_manager {
namespace test {

MockViewAssociate::MockViewAssociate() {}
MockViewAssociate::~MockViewAssociate() {}

void MockViewAssociate::Connect(
    mojo::InterfaceHandle<mojo::ui::ViewInspector> inspector,
    const ConnectCallback& callback) {
  connect_invokecount++;

  auto info = mojo::ui::ViewAssociateInfo::New();
  callback.Run(info.Pass());
}

void MockViewAssociate::ConnectToViewService(
    mojo::ui::ViewTokenPtr view_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {}

void MockViewAssociate::ConnectToViewTreeService(
    mojo::ui::ViewTreeTokenPtr view_tree_token,
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {}

}  // namespace test
}  // namespace view_manager
