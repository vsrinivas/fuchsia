// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be //
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_

#include <list>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>

#include "peridot/bin/context_engine/context_repository.h"

namespace modular {

class ContextReaderImpl : fuchsia::modular::ContextReader {
 public:
  ContextReaderImpl(
      fuchsia::modular::ComponentScope client_info,
      ContextRepository* repository,
      fidl::InterfaceRequest<fuchsia::modular::ContextReader> request);
  ~ContextReaderImpl() override;

 private:
  // |fuchsia::modular::ContextReader|
  void Subscribe(fuchsia::modular::ContextQuery query,
                 fidl::InterfaceHandle<fuchsia::modular::ContextListener>
                     listener) override;

  // |fuchsia::modular::ContextReader|
  void Get(fuchsia::modular::ContextQuery query,
           fuchsia::modular::ContextReader::GetCallback callback) override;

  fidl::Binding<fuchsia::modular::ContextReader> binding_;

  fuchsia::modular::SubscriptionDebugInfo debug_;
  ContextRepository* const repository_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextReaderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_
