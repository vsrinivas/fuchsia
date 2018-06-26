// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_ENGINE_IMPL_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_ENGINE_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "peridot/bin/context_engine/context_repository.h"
#include "peridot/bin/context_engine/debug.h"

namespace modular {

class EntityResolver;

class ContextReaderImpl;
class ContextWriterImpl;

class ContextEngineImpl : fuchsia::modular::ContextEngine {
 public:
  // Does not take ownership of |entity_resolver|.
  ContextEngineImpl(fuchsia::modular::EntityResolver* entity_resolver);
  ~ContextEngineImpl() override;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::modular::ContextEngine> request);

  fxl::WeakPtr<ContextDebugImpl> debug();

 private:
  // |fuchsia::modular::ContextEngine|
  void GetWriter(
      fuchsia::modular::ComponentScope client_info,
      fidl::InterfaceRequest<fuchsia::modular::ContextWriter> request) override;

  // |fuchsia::modular::ContextEngine|
  void GetReader(
      fuchsia::modular::ComponentScope client_info,
      fidl::InterfaceRequest<fuchsia::modular::ContextReader> request) override;

  // |fuchsia::modular::ContextEngine|
  void GetContextDebug(
      fidl::InterfaceRequest<fuchsia::modular::ContextDebug> request) override;

  fuchsia::modular::EntityResolver* const entity_resolver_;

  ContextRepository repository_;

  fidl::BindingSet<fuchsia::modular::ContextEngine> bindings_;

  std::vector<std::unique_ptr<ContextReaderImpl>> readers_;
  std::vector<std::unique_ptr<ContextWriterImpl>> writers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextEngineImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_ENGINE_IMPL_H_
