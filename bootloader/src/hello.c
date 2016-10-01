// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <efi/types.h>

EFIAPI efi_status efi_main(efi_handle img, efi_system_table* sys) {
    efi_simple_text_output_protocol* ConOut = sys->ConOut;
    ConOut->OutputString(ConOut, L"Hello, EFI World!\r\n");
    return EFI_SUCCESS;
}
