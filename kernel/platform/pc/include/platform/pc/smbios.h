// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/smbios/smbios.h>

void pc_init_smbios();

// Walk the known SMBIOS structures.  The callback will be called once for each
// structure found.
zx_status_t SmbiosWalkStructs(smbios::StructWalkCallback cb, void* ctx);
