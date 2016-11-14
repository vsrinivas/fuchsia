// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/resolver/resolver_impl.h"

namespace resolver {

void ResolverImpl::ResolveModule(fidl::String contract, document_store::DocumentPtr data,
    const ResolveModuleCallback& callback) override {
  // Return a null result because there is nothing to resolve to.
  ResultPtr result;
  callback(std::move(result));
}

} // namespace resolver
