// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! State maintained by the device layer.

use crate::{ip::device::state::DualStackIpDeviceState, Instant};

/// Device state.
///
/// `D` is the device-specific state.
pub(crate) struct DeviceState<D> {
    /// Device-specific state.
    pub(crate) device: D,
}

impl<D> DeviceState<D> {
    /// Creates a new `DeviceState` with a device-specific state `device`.
    pub(crate) fn new(device: D) -> Self {
        Self { device }
    }
}

/// State for a link-device that is also an IP device.
///
/// `D` is the link-specific state.
pub(crate) struct IpLinkDeviceState<I: Instant, D> {
    pub ip: DualStackIpDeviceState<I>,
    pub link: D,
}

impl<I: Instant, D> IpLinkDeviceState<I, D> {
    /// Create a new `IpLinkDeviceState` with a link-specific state `link`.
    pub(crate) fn new(link: D) -> Self {
        Self { ip: DualStackIpDeviceState::default(), link }
    }
}
