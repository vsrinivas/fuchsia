// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <zircon/compiler.h>
#include <zircon/nand/c/fidl.h>

__BEGIN_CDECLS

// Creates a ram_nand, returning the full path to the new device. The provided
// buffer for the path should be at least PATH_MAX characters long.
zx_status_t create_ram_nand(const zircon_nand_RamNandInfo* config, char* out_path);

// Destroys a ram_nand, given the name returned from create_ram_nand().
zx_status_t destroy_ram_nand(const char* ram_nand_path);

__END_CDECLS
