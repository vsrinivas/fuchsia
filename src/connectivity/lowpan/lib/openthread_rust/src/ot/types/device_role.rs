// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use num_derive::FromPrimitive;

/// Represents a Thread device role.
///
/// Functional equivalent of [`otsys::otDeviceRole`](crate::otsys::otDeviceRole).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, FromPrimitive)]
#[allow(missing_docs)]
pub enum DeviceRole {
    /// Functional equivalent of [`otsys::OT_DEVICE_ROLE_DISABLED`](crate::otsys::OT_DEVICE_ROLE_DISABLED).
    Disabled = OT_DEVICE_ROLE_DISABLED as isize,

    /// Functional equivalent of [`otsys::OT_DEVICE_ROLE_DETACHED`](crate::otsys::OT_DEVICE_ROLE_DETACHED).
    Detached = OT_DEVICE_ROLE_DETACHED as isize,

    /// Functional equivalent of [`otsys::OT_DEVICE_ROLE_CHILD`](crate::otsys::OT_DEVICE_ROLE_CHILD).
    Child = OT_DEVICE_ROLE_CHILD as isize,

    /// Functional equivalent of [`otsys::OT_DEVICE_ROLE_ROUTER`](crate::otsys::OT_DEVICE_ROLE_ROUTER).
    Router = OT_DEVICE_ROLE_ROUTER as isize,

    /// Functional equivalent of [`otsys::OT_DEVICE_ROLE_LEADER`](crate::otsys::OT_DEVICE_ROLE_LEADER).
    Leader = OT_DEVICE_ROLE_LEADER as isize,
}

impl DeviceRole {
    /// Returns true if the role is not disabled nor detached.
    pub fn is_active(&self) -> bool {
        match self {
            DeviceRole::Disabled | DeviceRole::Detached => false,
            _ => true,
        }
    }
}

impl From<otDeviceRole> for DeviceRole {
    fn from(x: otDeviceRole) -> Self {
        use num::FromPrimitive;
        Self::from_u32(x).expect(format!("Unknown otDeviceRole value: {}", x).as_str())
    }
}

impl From<DeviceRole> for otDeviceRole {
    fn from(x: DeviceRole) -> Self {
        x as otDeviceRole
    }
}
