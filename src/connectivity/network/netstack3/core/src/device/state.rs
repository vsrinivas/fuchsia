// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! State maintained by the device layer.

use core::fmt::Debug;

use crate::{ip::device::state::DualStackIpDeviceState, Instant};

/// Initialization status of a device.
#[derive(Debug, PartialEq, Eq)]
pub(crate) enum InitializationStatus {
    /// The device is not yet initialized and MUST NOT be used.
    Uninitialized,

    /// The device is currently being initialized and must only be used by
    /// the initialization methods.
    Initializing,

    /// The device is initialized and can operate as normal.
    Initialized,
}

impl Default for InitializationStatus {
    fn default() -> InitializationStatus {
        InitializationStatus::Uninitialized
    }
}

/// Common state across devices.
#[derive(Default)]
pub(crate) struct CommonDeviceState {
    /// The device's initialization status.
    initialization_status: InitializationStatus,
}

impl CommonDeviceState {
    pub(crate) fn is_initialized(&self) -> bool {
        self.initialization_status == InitializationStatus::Initialized
    }

    pub(crate) fn is_uninitialized(&self) -> bool {
        self.initialization_status == InitializationStatus::Uninitialized
    }

    pub(crate) fn set_initialization_status(&mut self, status: InitializationStatus) {
        self.initialization_status = status;
    }
}

/// Device state.
///
/// `D` is the device-specific state.
pub(crate) struct DeviceState<D> {
    /// Device-independant state.
    pub(crate) common: CommonDeviceState,

    /// Device-specific state.
    pub(crate) device: D,
}

impl<D> DeviceState<D> {
    /// Creates a new `DeviceState` with a device-specific state `device`.
    pub(crate) fn new(device: D) -> Self {
        Self { common: CommonDeviceState::default(), device }
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
