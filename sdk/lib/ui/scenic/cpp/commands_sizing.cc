// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/commands_sizing.h>
#include <zircon/types.h>

namespace scenic {

// TODO(fxb/24704): Generate this file using the FIDL JSON IR.

CommandSize MeasureCommand(const fuchsia::ui::scenic::Command& command) {
    // For now, we size all commands as if they were taking the max bytes and
    // max handles in order to force safe flushing.
    return CommandSize(ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES);
}

}  // namespace scenic
