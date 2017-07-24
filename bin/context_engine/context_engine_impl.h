// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/context/context_engine.fidl.h"
#include "apps/maxwell/services/context/context_publisher.fidl.h"
#include "apps/maxwell/services/context/context_reader.fidl.h"
#include "apps/maxwell/src/context_engine/context_repository.h"
#include "apps/maxwell/src/context_engine/debug.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace maxwell {

class ContextEngineImpl : public ContextEngine {
 public:
  ContextEngineImpl();
  ~ContextEngineImpl() override;

  ContextDebug* debug() { return &debug_; }

 private:
  // |ContextEngine|
  void GetPublisher(ComponentScopePtr scope,
                    fidl::InterfaceRequest<ContextPublisher> request) override;

  // |ContextEngine|
  void GetReader(ComponentScopePtr scope,
                   fidl::InterfaceRequest<ContextReader> request) override;

  ContextRepository repository_;
  ContextDebugImpl debug_;

  fidl::BindingSet<ContextPublisher, std::unique_ptr<ContextPublisher>>
      publisher_bindings_;
  fidl::BindingSet<ContextReader, std::unique_ptr<ContextReader>>
      reader_bindings_;

  fidl::BindingSet<ContextEngine> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextEngineImpl);
};

}  // namespace maxwell
