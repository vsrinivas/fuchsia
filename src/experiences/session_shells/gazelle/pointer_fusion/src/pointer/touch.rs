// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::*, fidl_fuchsia_ui_pointer as fptr};

impl PointerFusionState {
    pub(super) fn fuse_touch(&mut self, _event: fptr::TouchEvent) -> Vec<PointerEvent> {
        // TODO(fxb/110099): Fuse touch events.
        todo!("Fuse touch events");
    }
}
