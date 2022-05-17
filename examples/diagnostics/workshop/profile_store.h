// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_WORKSHOP_PROFILE_STORE_H_
#define EXAMPLES_DIAGNOSTICS_WORKSHOP_PROFILE_STORE_H_

#include <fuchsia/examples/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <map>
#include <memory>

#include "profile.h"

class ProfileStore : public fuchsia::examples::diagnostics::ProfileStore {
 public:
  fidl::InterfaceRequestHandler<fuchsia::examples::diagnostics::ProfileStore> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void AddBinding(fidl::InterfaceRequest<fuchsia::examples::diagnostics::ProfileStore> channel) {
    bindings_.AddBinding(this, std::move(channel), dispatcher_);
  }

  explicit ProfileStore(async_dispatcher_t* dispatcher);

  ~ProfileStore() override;

  void Open(std::string key,
            fidl::InterfaceRequest<fuchsia::examples::diagnostics::Profile> channel) override;

  void OpenReader(
      std::string key,
      fidl::InterfaceRequest<fuchsia::examples::diagnostics::ProfileReader> channel) override;

  void CreateOrOpen(
      std::string key,
      fidl::InterfaceRequest<::fuchsia::examples::diagnostics::Profile> channel) override;

  void Delete(std::string key, DeleteCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::examples::diagnostics::ProfileStore> bindings_;
  std::map<std::string, std::unique_ptr<Profile>> profiles_;
  async_dispatcher_t* dispatcher_;
};

#endif  // EXAMPLES_DIAGNOSTICS_WORKSHOP_PROFILE_STORE_H_
