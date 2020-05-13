// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include "src/ui/lib/escher/base/ownable.h"

namespace escher {

Reffable::~Reffable() { FX_DCHECK(ref_count_ == 0); }

#ifndef NDEBUG
void Reffable::Adopt() {
  FX_DCHECK(adoption_required_);
  FX_DCHECK(ref_count_ == 1);
  adoption_required_ = false;
}
#endif

}  // namespace escher
