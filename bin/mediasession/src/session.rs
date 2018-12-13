// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_mediasession::ControllerProxy;

/// Session is a tagged handle to some published media session.
#[allow(unused)]
pub struct Session {
    id: u64,
    controller: ControllerProxy,
}

impl Session {
    pub fn new(id: u64, controller: ControllerProxy) -> Self {
        Self { id, controller }
    }
}
