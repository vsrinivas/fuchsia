// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_INSPECT_SERVICE_IMPL_H_
#define GARNET_LIB_MACHINA_INSPECT_SERVICE_IMPL_H_

#include <fuchsia/cpp/machina.h>
#include "garnet/lib/machina/phys_mem.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"

namespace machina {

class InspectServiceImpl : public InspectService {
 public:
  InspectServiceImpl(component::ApplicationContext* application_context,
                     const PhysMem& phys_mem);

  // |InspectService|
  void FetchGuestMemory(FetchGuestMemoryCallback callback) override;
  void FetchGuestSerial(FetchGuestSerialCallback callback) override;

  zx::socket TakeSocket() { return fbl::move(server_socket_); }

 private:
  fidl::BindingSet<InspectService> bindings_;
  const zx::vmo vmo_;
  zx::socket server_socket_;
  zx::socket client_socket_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_INSPECT_SERVICE_IMPL_H_
