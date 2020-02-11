// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_TEST_BOARD_NAME_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_TEST_BOARD_NAME_H_

#include <zircon/types.h>

#include <fbl/string.h>

namespace audio::intel_hda {

// Get the name of the board we are running on, such as
// "Standard PC (Q35 + ICH9, 2009)" (qemu) or "Eve" (Pixelbook).
zx_status_t GetBoardName(fbl::String* result);

}  // namespace audio::intel_hda

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_INTEL_HDA_TEST_BOARD_NAME_H_
