// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string.h>
#include <zircon/types.h>

namespace audio::intel_hda {

// Get the name of the board we are running on, such as
// "Standard PC (Q35 + ICH9, 2009)" (qemu) or "Eve" (Pixelbook).
zx_status_t GetBoardName(fbl::String* result);

}  // namespace audio::intel_hda
