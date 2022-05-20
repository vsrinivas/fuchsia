// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_FIRMWARE_GIGABOOT_SRC_VARIABLE_H_
#define SRC_FIRMWARE_GIGABOOT_SRC_VARIABLE_H_

#include <zircon/compiler.h>

#include <efi/runtime-services.h>
#include <efi/types.h>

__BEGIN_CDECLS

efi_status set_bool(efi_runtime_services* runtime, char16_t* name, bool value);
efi_status get_bool(efi_runtime_services* runtime, char16_t* name, bool* value);

// Used by unit tests.
extern efi_guid kGigabootVendorGuid;

__END_CDECLS

#endif  // SRC_FIRMWARE_GIGABOOT_SRC_VARIABLE_H_
