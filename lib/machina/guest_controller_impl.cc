// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/guest_controller_impl.h"

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

GuestControllerImpl::GuestControllerImpl(
    component::ApplicationContext* application_context, const PhysMem& phys_mem)
    : vmo_(duplicate(phys_mem.vmo(), kVmoRights)) {
  zx_status_t status = zx::socket::create(0, &server_socket_, &client_socket_);
  FXL_CHECK(status == ZX_OK) << "Failed to create socket";

  application_context->outgoing()
      .AddPublicService<fuchsia::guest::GuestController>(
          [this](
              fidl::InterfaceRequest<fuchsia::guest::GuestController> request) {
            bindings_.AddBinding(this, std::move(request));
          });
}

void GuestControllerImpl::GetPhysicalMemory(
    GetPhysicalMemoryCallback callback) {
  callback(duplicate(vmo_, ZX_RIGHT_SAME_RIGHTS));
}

void GuestControllerImpl::GetSerial(GetSerialCallback callback) {
  callback(duplicate(client_socket_, ZX_RIGHT_SAME_RIGHTS));
}

void GuestControllerImpl::GetViewProvider(
    fidl::InterfaceRequest<::fuchsia::ui::views_v1::ViewProvider> request) {
  if (view_provider_ != nullptr) {
    view_provider_bindings_.AddBinding(view_provider_, std::move(request));
  }
}

}  // namespace machina
