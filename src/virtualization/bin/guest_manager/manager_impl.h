// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_MANAGER_IMPL_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_MANAGER_IMPL_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#include "src/virtualization/bin/guest_manager/realm_impl.h"

class ManagerImpl : public fuchsia::virtualization::Manager {
 public:
  ManagerImpl();

  ManagerImpl(const ManagerImpl&) = delete;
  ManagerImpl& operator=(const ManagerImpl&) = delete;

 private:
  // |fuchsia::virtualization::Manager|
  void Create(fidl::StringPtr label,
              fidl::InterfaceRequest<fuchsia::virtualization::Realm> env) override;
  void List(ListCallback callback) override;
  void Connect(uint32_t id, fidl::InterfaceRequest<fuchsia::virtualization::Realm> env) override;

  std::unique_ptr<sys::ComponentContext> context_;
  std::unordered_map<uint32_t, std::unique_ptr<RealmImpl>> environments_;
  fidl::BindingSet<fuchsia::virtualization::Manager> bindings_;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_MANAGER_IMPL_H_
