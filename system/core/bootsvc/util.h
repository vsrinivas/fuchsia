// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vector.h>
#include <lib/zx/vmo.h>

namespace bootsvc {

// Retrieves all bootdata VMOs from the startup handle table
fbl::Vector<zx::vmo> RetrieveBootdata();

// Path relative to /boot used for crashlogs.
extern const char* const kLastPanicFilePath;

}
