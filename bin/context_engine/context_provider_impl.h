// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>

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

  // We use a std::list<> here instead of a std::vector<> since we capture
  // iterators in |listeners_| for removing elements in our connection
  // error handler.
  std::list<ContextListenerPtr> listeners_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ContextProviderImpl);
};

}  // namespace maxwell
