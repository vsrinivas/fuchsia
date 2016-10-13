// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/resource.h"

#include "escher/impl/escher_impl.h"

namespace escher {
namespace impl {

Resource::Resource(EscherImpl* escher) : escher_(escher) {
  if (escher_)
    escher_->IncrementResourceCount();
}

Resource::~Resource() {
  if (escher_)
    escher_->DecrementResourceCount();
}

}  // namespace impl
}  // namespace escher
