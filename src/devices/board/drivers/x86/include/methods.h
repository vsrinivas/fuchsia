// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_METHODS_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_METHODS_H_

#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <acpica/acpi.h>
#include <acpica/actypes.h>

#define ACPI_UUID_SIZE 16u

// TODO(fxbug.dev/78349): delete these methods once users are in their own drivers.

// Call the ACPI _OSC method on a device object.
//
// Returns values:
//  ZX_ERR_INVALID_ARGS: dwords_in or dwords_out are NULL, dword_cnt is less than 2,
//                       or the UUID is incorrectly formatted.
//  ZX_ERR_BUFFER_TOO_SMALL: The dwords provided are not large enough for the result
//                           from the method.
//  ZX_ERR_INTERNAL: The method returned an error bit that should be checked in dwords_out[0]
zx_status_t acpi_osc_call(ACPI_HANDLE dev_obj, const char* uuid_str, uint64_t revision,
                          size_t dword_cnt, uint32_t* dwords_in, uint32_t* dwords_out,
                          bool* bit_masked);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_METHODS_H_
