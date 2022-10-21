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
        const SLAVE = uapi::MS_SLAVE;
        const SHARED = uapi::MS_SHARED;
        const PRIVATE = uapi::MS_PRIVATE;

        /// Flags that can be stored in Mount state.
        const STORED_FLAGS = Self::RDONLY.bits | Self::NOEXEC.bits | Self::NOSUID.bits | Self::NODEV.bits | Self::SHARED.bits;
    }
}
