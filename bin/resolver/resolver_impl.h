// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/services/resolver.fidl.h"

#include "apps/modular/services/document_store/document.fidl.h"

#include "lib/fidl/cpp/bindings/binding.h"

namespace resolver {

class ResolverImpl : public Resolver {
 public:
  ResolverImpl() {}

  void ResolveModule(fidl::String contract, document_store::DocumentPtr data,
      const ResolveModuleCallback& callback) override;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(ResolverImpl);
};

} // namespace resolver
