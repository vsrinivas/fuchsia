// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;

use crate::types::uapi;

bitflags! {
    pub struct MountFlags: u32 {
        const RDONLY = uapi::MS_RDONLY;
        const NOEXEC = uapi::MS_NOEXEC;
        const NOSUID = uapi::MS_NOSUID;
        const NODEV = uapi::MS_NODEV;
        const BIND = uapi::MS_BIND;
        const REC = uapi::MS_REC;
    }
}
