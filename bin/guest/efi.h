// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_EFI_H_
#define GARNET_BIN_GUEST_EFI_H_

#include <zircon/types.h>

zx_status_t read_efi(const uintptr_t first_page,
                     uintptr_t* guest_ip,
                     uintptr_t* kernel_off,
                     size_t* kernel_len);

#endif  // GARNET_BIN_GUEST_EFI_H_
