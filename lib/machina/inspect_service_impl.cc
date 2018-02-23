// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/inspect_service_impl.h"

#include "lib/fxl/logging.h"

static constexpr zx_rights_t kVmoRights =
    ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHTS_BASIC;

template <typename T>
static T duplicate(const T& handle, zx_rights_t rights) {
  T handle_out;
  zx_status_t status = handle.duplicate(rights, &handle_out);
  FXL_CHECK(status == ZX_OK) << "Failed to duplicate handle";
  return handle_out;
}

namespace machina {

InspectServiceImpl::InspectServiceImpl(
    app::ApplicationContext* application_context,
    const PhysMem& phys_mem)
    : vmo_(duplicate(phys_mem.vmo(), kVmoRights)) {
  zx_status_t status = zx::socket::create(0, &server_socket_, &client_socket_);
  FXL_CHECK(status == ZX_OK) << "Failed to create socket";

  application_context->outgoing_services()->AddService<InspectService>(
      [this](f1dl::InterfaceRequest<InspectService> request) {
        bindings_.AddBinding(this, std::move(request));
      });
}

void InspectServiceImpl::FetchGuestMemory(
    const FetchGuestMemoryCallback& callback) {
  callback(duplicate(vmo_, ZX_RIGHT_SAME_RIGHTS));
}

void InspectServiceImpl::FetchGuestSerial(
    const FetchGuestSerialCallback& callback) {
  callback(duplicate(client_socket_, ZX_RIGHT_SAME_RIGHTS));
}

}  // namespace machina
