// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/acpi_lite/structures.h"

#include <endian.h>
#include <lib/acpi_lite/internal.h>
#include <stdint.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Write the signature into the given buffer.
//
// Buffer must have a length of at least 5.
void AcpiSignature::WriteToBuffer(char* buffer) const {
  memcpy(buffer, &value, sizeof(AcpiSignature));
  buffer[sizeof(AcpiSignature)] = 0;
}
