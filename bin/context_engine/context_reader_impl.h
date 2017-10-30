// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_

#include <list>

#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/user_intelligence/fidl/scope.fidl.h"
#include "peridot/bin/context_engine/context_repository.h"

namespace maxwell {

class ContextReaderImpl : ContextReader {
 public:
  ContextReaderImpl(ComponentScopePtr client,
                    ContextRepository* repository,
                    fidl::InterfaceRequest<ContextReader> request);
  ~ContextReaderImpl() override;

 private:
  // |ContextReader|
  void Subscribe(ContextQueryPtr query,
                 fidl::InterfaceHandle<ContextListener> listener) override;

  fidl::Binding<ContextReader> binding_;

  SubscriptionDebugInfoPtr debug_;
  ContextRepository* const repository_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextReaderImpl);
};

}  // namespace maxwell

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_
