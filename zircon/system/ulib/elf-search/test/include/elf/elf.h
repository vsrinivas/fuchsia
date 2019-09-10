// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <stddef.h>

__BEGIN_CDECLS

// Load an extra ELF file image into a process VMAR.
// This is a convenience wrapper around the elfload library.
zx_status_t elf_load_extra(zx_handle_t vmar, zx_handle_t vmo, zx_vaddr_t* base, zx_vaddr_t* entry);

__END_CDECLS
