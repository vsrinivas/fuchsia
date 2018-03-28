// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_ENGINE_IMPL_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_ENGINE_IMPL_H_

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding_set.h"
#include "peridot/bin/context_engine/context_repository.h"
#include "peridot/bin/context_engine/debug.h"

namespace modular {

class EntityResolver;

class ContextReaderImpl;
class ContextWriterImpl;

class ContextEngineImpl : ContextEngine {
 public:
  // Does not take ownership of |entity_resolver|.
  ContextEngineImpl(EntityResolver* entity_resolver);
  ~ContextEngineImpl() override;

  void AddBinding(fidl::InterfaceRequest<ContextEngine> request);

  fxl::WeakPtr<ContextDebugImpl> debug();

 private:
  // |ContextEngine|
  void GetWriter(ComponentScope client_info,
                 fidl::InterfaceRequest<ContextWriter> request) override;

  // |ContextEngine|
  void GetReader(ComponentScope client_info,
                 fidl::InterfaceRequest<ContextReader> request) override;

  // |ContextEngine|
  void GetContextDebug(fidl::InterfaceRequest<ContextDebug> request) override;

  EntityResolver* const entity_resolver_;

  ContextRepository repository_;

  fidl::BindingSet<ContextEngine> bindings_;

  std::vector<std::unique_ptr<ContextReaderImpl>> readers_;
  std::vector<std::unique_ptr<ContextWriterImpl>> writers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextEngineImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_ENGINE_IMPL_H_
