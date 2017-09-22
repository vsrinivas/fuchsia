// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/context/fidl/context_engine.fidl.h"
#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/context/fidl/context_writer.fidl.h"
#include "peridot/bin/context_engine/context_repository.h"
#include "peridot/bin/context_engine/debug.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class ContextReaderImpl;
class ContextWriterImpl;

class ContextEngineImpl : ContextEngine {
 public:
  ContextEngineImpl();
  ~ContextEngineImpl() override;

  void AddBinding(fidl::InterfaceRequest<ContextEngine> request);

 private:
  // |ContextEngine|
  void GetWriter(ComponentScopePtr client_info,
                 fidl::InterfaceRequest<ContextWriter> request) override;

  // |ContextEngine|
  void GetReader(ComponentScopePtr client_info,
                 fidl::InterfaceRequest<ContextReader> request) override;

  // |ContextEngine|
  void GetContextDebug(fidl::InterfaceRequest<ContextDebug> request) override;

  ContextRepository repository_;

  fidl::BindingSet<ContextEngine> bindings_;

  std::vector<std::unique_ptr<ContextReaderImpl>> readers_;
  std::vector<std::unique_ptr<ContextWriterImpl>> writers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextEngineImpl);
};

}  // namespace maxwell
