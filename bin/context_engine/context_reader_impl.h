// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>

#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/services/user/scope.fidl.h"
#include "apps/maxwell/src/context_engine/context_repository.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace maxwell {

class ContextReaderImpl : ContextReader {
 public:
  ContextReaderImpl(ComponentScopePtr client, ContextRepository* repository,
      fidl::InterfaceRequest<ContextReader> request);
  ~ContextReaderImpl() override;

 private:
  // |ContextReader|
  void Subscribe(
      ContextQueryPtr query,
      fidl::InterfaceHandle<ContextListener> listener) override;

  fidl::Binding<ContextReader> binding_;

  SubscriptionDebugInfoPtr debug_;
  ContextRepository* const repository_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextReaderImpl);
};

}  // namespace maxwell
