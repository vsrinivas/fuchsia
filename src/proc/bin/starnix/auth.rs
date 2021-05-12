// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::types::*;

#[derive(Default, Clone)]
pub struct Credentials {
    pub uid: uid_t,
    pub gid: uid_t,
    pub euid: uid_t,
    pub egid: uid_t,
}
