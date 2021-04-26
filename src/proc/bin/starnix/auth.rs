// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::uapi::*;

#[derive(Default)]
pub struct Credentials {
    pub uid: uid_t,
    pub gid: uid_t,
    pub euid: uid_t,
    pub egid: uid_t,
}

impl Credentials {
    #[cfg(test)] // Currently used only by tests.
    pub fn root() -> Credentials {
        Credentials { uid: 0, gid: 0, euid: 0, egid: 0 }
    }
}
