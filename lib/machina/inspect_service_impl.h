// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_INSPECTOR_SERVICE_IMPL_H_
#define GARNET_LIB_MACHINA_INSPECTOR_SERVICE_IMPL_H_

#include "garnet/lib/machina/fidl/inspect.fidl.h"
#include "garnet/lib/machina/phys_mem.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace machina {

class InspectServiceImpl : public InspectService {
 public:
  InspectServiceImpl(app::ApplicationContext* application_context,
                     const PhysMem& phys_mem);

  // |InspectService|
  void FetchGuestMemory(const FetchGuestMemoryCallback& callback) override;

 private:
  fidl::BindingSet<InspectService> bindings_;
  const zx::vmo vmo_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_INSPECTOR_SERVICE_IMPL_H_
