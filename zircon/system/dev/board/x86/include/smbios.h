// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

// Populates the given buffer with the board name.  If the board name is larger
// than the buffer, ZX_ERR_BUFFER_TOO_SMALL is returned, and *board_name_actual
// refers to the needed size.  These lengths include a trailing null-byte
zx_status_t smbios_get_board_name(char* board_name_buffer, size_t board_name_size,
                                  size_t* board_name_actual);

// Check if we consider the given product name to be valid.
bool smbios_product_name_is_valid(const char* product_name);
