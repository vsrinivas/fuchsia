// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GUEST_CONTROLLER_IMPL_H_
#define GARNET_LIB_MACHINA_GUEST_CONTROLLER_IMPL_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>

#include "garnet/lib/machina/phys_mem.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"

namespace machina {

// Provides an implementation of the |fuchsi::guest::GuestController| interface.
// This exposes some guest services over FIDL.
class GuestControllerImpl : public fuchsia::guest::GuestController {
 public:
  GuestControllerImpl(component::StartupContext* startup_context,
                      const PhysMem& phys_mem);

  void set_view_provider(::fuchsia::ui::views_v1::ViewProvider* view_provider) {
    view_provider_ = view_provider;
  }

  // Extracts the socket handle to be used for the host end of serial
  // communication. The other end of this socket will be provided to clients
  // via |GetSerial|.
  zx::socket TakeSocket() { return std::move(server_socket_); }

  // |fuchsia::guest::GuestController|
  void GetPhysicalMemory(GetPhysicalMemoryCallback callback) override;
  void GetSerial(GetSerialCallback callback) override;
  void GetViewProvider(GetViewProviderCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::guest::GuestController> bindings_;
  fidl::BindingSet<::fuchsia::ui::views_v1::ViewProvider>
      view_provider_bindings_;

  const zx::vmo vmo_;
  zx::socket server_socket_;
  zx::socket client_socket_;
  ::fuchsia::ui::views_v1::ViewProvider* view_provider_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GUEST_CONTROLLER_IMPL_H_
