// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_SERVICES_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_SERVICES_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/svc/cpp/service_provider_bridge.h>

#include "src/virtualization/bin/guest_manager/guest_vsock_endpoint.h"

class GuestServices : public fuchsia::virtualization::GuestConfigProvider {
 public:
  explicit GuestServices(fuchsia::virtualization::GuestConfig cfg);

  fuchsia::sys::ServiceListPtr ServeDirectory();

 private:
  // |fuchsia::virtualization::GuestConfigProvider|
  void Get(GetCallback callback) override;

  component::ServiceProviderBridge services_;
  fidl::Binding<fuchsia::virtualization::GuestConfigProvider> binding_{this};
  fuchsia::virtualization::GuestConfig cfg_;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_SERVICES_H_
