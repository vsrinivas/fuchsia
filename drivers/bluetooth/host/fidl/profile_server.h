// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/bluetooth/bredr/cpp/fidl.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"

namespace bthost {

// Implements the bredr::Profile FIDL interface.
class ProfileServer
    : public AdapterServerBase<fuchsia::bluetooth::bredr::Profile> {
 public:
  ProfileServer(
      fxl::WeakPtr<::btlib::gap::Adapter> adapter,
      fidl::InterfaceRequest<fuchsia::bluetooth::bredr::Profile> request);
  ~ProfileServer() override;

 private:
  // fuchsia::bluetooth::bredr::Profile overrides:
  void AddService(fuchsia::bluetooth::bredr::ServiceDefinition definition,
                  fuchsia::bluetooth::bredr::SecurityLevel sec_level,
                  bool devices, AddServiceCallback callback) override;
  void DisconnectClient(::fidl::StringPtr device_id,
                        ::fidl::StringPtr service_id) override;
  void RemoveService(::fidl::StringPtr service_id) override;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<ProfileServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProfileServer);
};

}  // namespace bthost
