// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/component/fidl/component.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/resolver/fidl/resolver.fidl.h"

namespace resolver {

class ResolverImpl : public Resolver {
 public:
  ResolverImpl(component::ComponentIndexPtr component_index)
      : component_index_(std::move(component_index)) {}

  void ResolveModules(const fidl::String& contract,
                      const fidl::String& json_data,
                      const ResolveModulesCallback& callback) override;

 private:
  component::ComponentIndexPtr component_index_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ResolverImpl);
};

}  // namespace resolver
