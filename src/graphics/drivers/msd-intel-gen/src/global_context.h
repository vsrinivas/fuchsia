// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GLOBAL_CONTEXT_H
#define GLOBAL_CONTEXT_H

#include "address_space.h"
#include "hardware_status_page.h"
#include "msd_intel_context.h"

// Deprecated GlobalContext; remove this
class GlobalContext : public MsdIntelContext {
 public:
  GlobalContext(std::shared_ptr<AddressSpace> address_space)
      : MsdIntelContext(std::move(address_space)) {}
};

#endif  // GLOBAL_CONTEXT_H
