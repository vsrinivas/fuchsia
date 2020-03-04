// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_PAVER_VALIDATION_H_
#define ZIRCON_SYSTEM_ULIB_PAVER_VALIDATION_H_

#include <zircon/errors.h>
#include <zircon/status.h>

#include <fbl/span.h>

#include "device-partitioner.h"

namespace paver {

// Perform some basic safety checks to ensure the given payload is a valid ZBI
// for the given architecture.
bool IsValidKernelZbi(Arch arch, fbl::Span<const uint8_t> data);

}  // namespace paver

#endif  // ZIRCON_SYSTEM_ULIB_PAVER_VALIDATION_H_
