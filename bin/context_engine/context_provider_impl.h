// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/services/context/context_provider.fidl.h"

namespace maxwell {

class ContextRepository;

class ContextProviderImpl : public ContextProvider {
 public:
  ContextProviderImpl(ContextRepository* repository);
  ~ContextProviderImpl() override;

 private:
  // |ContextProvider|
  void Subscribe(ContextQueryPtr query,
                 fidl::InterfaceHandle<ContextListener> listener) override;

  ContextRepository* repository_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextProviderImpl);
};

}  // namespace maxwell
