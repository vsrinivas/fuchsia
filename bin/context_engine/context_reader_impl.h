// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE file.

#ifndef PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_
#define PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_

#include <list>

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding.h"
#include "peridot/bin/context_engine/context_repository.h"

namespace modular {

class ContextReaderImpl : ContextReader {
 public:
  ContextReaderImpl(ComponentScope client,
                    ContextRepository* repository,
                    fidl::InterfaceRequest<ContextReader> request);
  ~ContextReaderImpl() override;

 private:
  // |ContextReader|
  void Subscribe(ContextQuery query,
                 fidl::InterfaceHandle<ContextListener> listener) override;

  // |ContextReader|
  void Get(
      ContextQuery query,
      ContextReader::GetCallback callback) override;

  fidl::Binding<ContextReader> binding_;

  SubscriptionDebugInfo debug_;
  ContextRepository* const repository_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ContextReaderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_CONTEXT_ENGINE_CONTEXT_READER_IMPL_H_
