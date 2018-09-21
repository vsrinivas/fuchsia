// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/instance_controller_impl.h"

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

InstanceControllerImpl::InstanceControllerImpl(
    component::StartupContext* context, const machina::PhysMem& phys_mem)
    : vmo_(duplicate(phys_mem.vmo(), kVmoRights)) {
  zx_status_t status = zx::socket::create(0, &server_socket_, &client_socket_);
  FXL_CHECK(status == ZX_OK) << "Failed to create socket";

  context->outgoing().AddPublicService(bindings_.GetHandler(this));
}

void InstanceControllerImpl::GetSerial(GetSerialCallback callback) {
  callback(duplicate(client_socket_, ZX_RIGHT_SAME_RIGHTS));
}

void InstanceControllerImpl::GetViewProvider(GetViewProviderCallback callback) {
  if (view_provider_ == nullptr) {
    // AddBinding gives a "valid" handle to a null ImplPtr, so we explicitly
    // pass a nullptr to the callback here.
    callback(nullptr);
    return;
  }
  callback(view_provider_bindings_.AddBinding(view_provider_));
}

void InstanceControllerImpl::GetInputDispatcher(
    fidl::InterfaceRequest<fuchsia::ui::input::InputDispatcher>
        input_dispatcher_request) {
  FXL_DCHECK(input_dispatcher_ != nullptr);
  input_dispatcher_bindings_.AddBinding(input_dispatcher_,
                                        std::move(input_dispatcher_request));
}
