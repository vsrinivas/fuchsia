// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/inspect_service_impl.h"

#include "lib/fxl/logging.h"

static constexpr zx_rights_t kVmoRights =
    ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHTS_BASIC;

static zx::vmo duplicate(const zx::vmo& vmo, zx_rights_t rights) {
  zx::vmo vmo_out;
  zx_status_t status = vmo.duplicate(rights, &vmo_out);
  FXL_CHECK(status == ZX_OK) << "Failed to duplicate guest memory";
  return vmo_out;
}

namespace machina {

InspectServiceImpl::InspectServiceImpl(
    app::ApplicationContext* application_context,
    const PhysMem& phys_mem)
    : vmo_(duplicate(phys_mem.vmo(), kVmoRights)) {
  application_context->outgoing_services()->AddService<InspectService>(
      [this](fidl::InterfaceRequest<InspectService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void InspectServiceImpl::FetchGuestMemory(
    const FetchGuestMemoryCallback& callback) {
  callback(duplicate(vmo_, ZX_RIGHT_SAME_RIGHTS));
}

}  // namespace machina
