// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vector.h>
#include <fs/vfs.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>

namespace bootsvc {

// Retrieves all bootdata VMOs from the startup handle table.
fbl::Vector<zx::vmo> RetrieveBootdata();

// Create a connection to a |vnode| in a |vfs|.
zx_status_t CreateVnodeConnection(fs::Vfs* vfs, fbl::RefPtr<fs::Vnode> vnode, zx::channel* out);

// Path relative to /boot used for crashlogs.
extern const char* const kLastPanicFilePath;

} // namespace bootsvc
