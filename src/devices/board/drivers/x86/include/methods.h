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

zx_status_t acpi_bbn_call(ACPI_HANDLE dev_obj, uint8_t* out_bbn);
zx_status_t acpi_crt_call(ACPI_HANDLE dev_obj, uint64_t* out);

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
zx_status_t acpi_psv_call(ACPI_HANDLE dev_obj, uint64_t* out);
zx_status_t acpi_seg_call(ACPI_HANDLE dev_obj, uint16_t* out_seg);
zx_status_t acpi_tmp_call(ACPI_HANDLE dev_obj, uint64_t* out);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_METHODS_H_
