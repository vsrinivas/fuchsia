// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/base/ownable.h"

#include "lib/fxl/logging.h"

namespace escher {

Reffable::~Reffable() { FXL_DCHECK(ref_count_ == 0); }

#ifndef NDEBUG
void Reffable::Adopt() {
  FXL_DCHECK(adoption_required_);
  FXL_DCHECK(ref_count_ == 1);
  adoption_required_ = false;
}
#endif

}  // namespace escher
