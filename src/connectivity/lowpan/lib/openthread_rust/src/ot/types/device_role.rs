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
    /// Functional equivalent of [`otsys::otDeviceRole_OT_DEVICE_ROLE_DISABLED`](crate::otsys::otDeviceRole_OT_DEVICE_ROLE_DISABLED).
    Disabled = otDeviceRole_OT_DEVICE_ROLE_DISABLED as isize,

    /// Functional equivalent of [`otsys::otDeviceRole_OT_DEVICE_ROLE_DETACHED`](crate::otsys::otDeviceRole_OT_DEVICE_ROLE_DETACHED).
    Detached = otDeviceRole_OT_DEVICE_ROLE_DETACHED as isize,

    /// Functional equivalent of [`otsys::otDeviceRole_OT_DEVICE_ROLE_CHILD`](crate::otsys::otDeviceRole_OT_DEVICE_ROLE_CHILD).
    Child = otDeviceRole_OT_DEVICE_ROLE_CHILD as isize,

    /// Functional equivalent of [`otsys::otDeviceRole_OT_DEVICE_ROLE_ROUTER`](crate::otsys::otDeviceRole_OT_DEVICE_ROLE_ROUTER).
    Router = otDeviceRole_OT_DEVICE_ROLE_ROUTER as isize,

    /// Functional equivalent of [`otsys::otDeviceRole_OT_DEVICE_ROLE_LEADER`](crate::otsys::otDeviceRole_OT_DEVICE_ROLE_LEADER).
    Leader = otDeviceRole_OT_DEVICE_ROLE_LEADER as isize,
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
