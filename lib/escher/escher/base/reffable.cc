// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/base/ownable.h"

#include "lib/ftl/logging.h"

namespace escher {

Reffable::~Reffable() {
  FTL_DCHECK(ref_count_ == 0);
}

#ifndef NDEBUG
void Reffable::Adopt() {
  FTL_DCHECK(adoption_required_);
  FTL_DCHECK(ref_count_ == 1);
  adoption_required_ = false;
}
#endif

}  // namespace escher
