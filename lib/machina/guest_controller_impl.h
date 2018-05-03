// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_GUEST_CONTROLLER_IMPL_H_
#define GARNET_LIB_MACHINA_GUEST_CONTROLLER_IMPL_H_

#include <fuchsia/cpp/guest.h>
#include <fuchsia/cpp/views_v1.h>

#include "garnet/lib/machina/phys_mem.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"

namespace machina {

// Provides an implementation of the |guest::GuestController| interface. This
// exposes some guest services over FIDL.
class GuestControllerImpl : public guest::GuestController {
 public:
  GuestControllerImpl(component::ApplicationContext* application_context,
                      const PhysMem& phys_mem);

  void set_view_provider(views_v1::ViewProvider* view_provider) {
    view_provider_ = view_provider;
  }

  // Extracts the socket handle to be used for the host end of serial
  // communication. The other end of this socket will be provided to clients
  // via |FetchGuestSerial|.
  zx::socket TakeSocket() { return fbl::move(server_socket_); }

  // |guest::GuestController|
  void FetchGuestMemory(FetchGuestMemoryCallback callback) override;
  void FetchGuestSerial(FetchGuestSerialCallback callback) override;
  void FetchViewProvider(
      fidl::InterfaceRequest<views_v1::ViewProvider> view_provider) override;

 private:
  fidl::BindingSet<guest::GuestController> bindings_;
  fidl::BindingSet<views_v1::ViewProvider> view_provider_bindings_;

  const zx::vmo vmo_;
  zx::socket server_socket_;
  zx::socket client_socket_;
  views_v1::ViewProvider* view_provider_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GUEST_CONTROLLER_IMPL_H_
