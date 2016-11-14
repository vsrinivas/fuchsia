// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/resolver/resolver_impl.h"

namespace resolver {

void ResolverImpl::ResolveModules(const fidl::String& contract,
                                  document_store::DocumentPtr data,
                                  const ResolveModulesCallback& callback) {
  // Return a null result because there is nothing to resolve to.
  fidl::Array<ResultPtr> results(fidl::Array<ResultPtr>::New(0));
  callback(std::move(results));
}

}  // namespace resolver
