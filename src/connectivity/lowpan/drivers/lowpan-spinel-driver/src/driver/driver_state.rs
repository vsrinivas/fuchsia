// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::spinel::*;

use fidl_fuchsia_lowpan::Identity;
use std::fmt::Debug;
use std::ops::Deref;

/// Structure which contains all of the current state of the driver.
///
/// The fields of this struct are kept in a mutex at `SpinelDriver.driver_state`.
/// The condition `SpinelDriver.driver_state_condition` must be triggered
/// after any updates.
#[derive(Debug)]
pub(super) struct DriverState {
    pub(super) init_state: InitState,
    pub(super) connectivity_state: ConnectivityState,

    /// Spinel capabilities supported by this device.
    ///
    /// This is updated exclusively at initialization.
    pub(super) caps: Vec<Cap>,

    /// The current role for this device on the current network.
    ///
    /// This is kept in sync by [`SpinelDriver::on_prop_value_is()`].
    pub(super) role: Role,

    /// The current network identity configured on the device.
    ///
    /// This is kept in sync by [`SpinelDriver::on_prop_value_is()`].
    pub(super) identity: Identity,

    /// The current set of addresses configured for this interface.
    pub(super) address_table: AddressTable,

    /// The current link-local address
    pub(super) link_local_addr: std::net::Ipv6Addr,

    /// The current mesh-local address
    pub(super) mesh_local_addr: std::net::Ipv6Addr,
}

impl Clone for DriverState {
    fn clone(&self) -> Self {
        DriverState {
            init_state: self.init_state.clone(),
            connectivity_state: self.connectivity_state.clone(),
            caps: self.caps.clone(),
            role: self.role.clone(),
            identity: self.identity.clone(),
            address_table: self.address_table.clone(),
            link_local_addr: self.link_local_addr.clone(),
            mesh_local_addr: self.mesh_local_addr.clone(),
        }
    }
}

impl PartialEq for DriverState {
    fn eq(&self, other: &Self) -> bool {
        self.init_state.eq(&other.init_state)
            && self.connectivity_state.eq(&other.connectivity_state)
            && self.caps.eq(&other.caps)
            && self.role.eq(&other.role)
            && self.identity.net_type.eq(&other.identity.net_type)
            && self.identity.channel.eq(&other.identity.channel)
            && self.identity.panid.eq(&other.identity.panid)
            && self.identity.raw_name.eq(&other.identity.raw_name)
            && self.identity.xpanid.eq(&other.identity.xpanid)
            && self.address_table.eq(&other.address_table)
            && self.link_local_addr.eq(&other.link_local_addr)
            && self.mesh_local_addr.eq(&other.mesh_local_addr)
    }
}

impl Eq for DriverState {}

impl Default for DriverState {
    fn default() -> Self {
        DriverState {
            init_state: Default::default(),
            connectivity_state: ConnectivityState::Inactive,
            caps: Default::default(),
            role: Role::Detached,
            identity: Identity::EMPTY,
            address_table: Default::default(),
            link_local_addr: std::net::Ipv6Addr::UNSPECIFIED,
            mesh_local_addr: std::net::Ipv6Addr::UNSPECIFIED,
        }
    }
}

impl DriverState {
    /// Convenience method for calling `init_state.is_initialized()`.
    pub fn is_initialized(&self) -> bool {
        self.init_state.is_initialized()
    }

    /// Convenience method for calling `init_state.is_initializing()`.
    pub fn is_initializing(&self) -> bool {
        self.init_state.is_initializing()
    }

    /// Convenience method for calling `init_state.is_ready()`.
    #[allow(dead_code)]
    pub fn is_ready(&self) -> bool {
        self.connectivity_state.is_ready()
    }

    /// Convenience method for calling `init_state.is_active()`.
    #[allow(dead_code)]
    pub fn is_active(&self) -> bool {
        self.connectivity_state.is_active()
    }

    /// Convenience method for checking if a cap is set.
    #[allow(dead_code)]
    pub fn has_cap(&self, cap: Cap) -> bool {
        self.caps.contains(&cap)
    }

    /// Prepares the driver state for (re-)initialization.
    pub fn prepare_for_init(&mut self) -> Option<ConnectivityState> {
        let old_state = self.connectivity_state;

        match self.init_state {
            InitState::Initialized | InitState::RecoverFromReset => {
                self.init_state = Default::default();
            }
            _ => {}
        }

        self.caps = Default::default();
        self.role = Role::Detached;

        if old_state.is_invalid_during_initialization() {
            self.connectivity_state = ConnectivityState::Attaching;
            return Some(old_state);
        }

        None
    }

    pub fn addr_is_mesh_local(&self, addr: &std::net::Ipv6Addr) -> bool {
        &addr.segments()[0..3] == &self.mesh_local_addr.segments()[0..3]
    }
}

/// Extension trait for adding some helper methods to
/// [`::fidl_fuchsia_lowpan::ConectivityState`].
///
/// See `doc/lowpan-connectivity-state.svg` for a diagram.
pub(super) trait ConnectivityStateExt {
    /// Indicates if the current state is considered 'ready' or not.
    fn is_ready(&self) -> bool;

    /// Indicates if the current state is considered 'active' or not.
    fn is_active(&self) -> bool;

    /// Indicates if the current state is considered simultaneously
    /// 'active' and 'ready' (could also be described as 'online')
    fn is_active_and_ready(&self) -> bool;

    /// Returns true if the current state is invalid during initialization.
    fn is_invalid_during_initialization(&self) -> bool;

    /// Returns true if the current state allows network packets to be sent and received.
    fn is_online(&self) -> bool;

    /// Returns the next state to switch to if the device
    /// is *activated* (for example, by a call to `set_active(true)`)
    fn activated(&self) -> ConnectivityState;

    /// Returns the next state to switch to if the device
    /// is *deactivated* (for example, by a call to `set_active(false)`).
    fn deactivated(&self) -> ConnectivityState;

    /// Returns the next state to switch to if the device
    /// is *provisioned* (for example, by a call to `provision_network()`).
    fn provisioned(&self) -> ConnectivityState;

    /// Returns the next state to switch to if the device
    /// is *unprovisioned* (for example, by a call to `leave_network()`).
    fn unprovisioned(&self) -> ConnectivityState;

    /// Returns the next state to switch to if the device
    /// role changes.
    fn role_updated(&self, role: Role) -> ConnectivityState;
}

impl ConnectivityStateExt for ConnectivityState {
    fn is_ready(&self) -> bool {
        match self {
            ConnectivityState::Inactive => false,
            ConnectivityState::Offline => false,

            ConnectivityState::Ready => true,
            ConnectivityState::Attaching => true,
            ConnectivityState::Attached => true,
            ConnectivityState::Isolated => true,
            ConnectivityState::Commissioning => true,
        }
    }

    fn is_active(&self) -> bool {
        match self {
            ConnectivityState::Inactive => false,
            ConnectivityState::Ready => false,

            ConnectivityState::Offline => true,
            ConnectivityState::Attaching => true,
            ConnectivityState::Attached => true,
            ConnectivityState::Isolated => true,
            ConnectivityState::Commissioning => true,
        }
    }

    fn is_active_and_ready(&self) -> bool {
        self.is_active() && self.is_ready()
    }

    fn is_online(&self) -> bool {
        match self {
            ConnectivityState::Inactive => false,
            ConnectivityState::Ready => false,
            ConnectivityState::Offline => false,
            ConnectivityState::Attaching => false,

            ConnectivityState::Attached => true,
            ConnectivityState::Isolated => true,
            ConnectivityState::Commissioning => true,
        }
    }

    fn is_invalid_during_initialization(&self) -> bool {
        self.is_online()
    }

    fn activated(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Inactive => ConnectivityState::Offline,
            ConnectivityState::Ready => ConnectivityState::Attaching,

            state => state.clone(),
        }
    }

    fn deactivated(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Offline => ConnectivityState::Inactive,
            ConnectivityState::Commissioning => ConnectivityState::Inactive,

            ConnectivityState::Attaching => ConnectivityState::Ready,
            ConnectivityState::Attached => ConnectivityState::Ready,
            ConnectivityState::Isolated => ConnectivityState::Ready,

            state => state.clone(),
        }
    }

    fn provisioned(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Inactive => ConnectivityState::Ready,
            ConnectivityState::Offline => ConnectivityState::Attaching,
            ConnectivityState::Commissioning => ConnectivityState::Attaching,

            state => state.clone(),
        }
    }

    fn unprovisioned(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Ready => ConnectivityState::Inactive,

            ConnectivityState::Attaching => ConnectivityState::Offline,
            ConnectivityState::Attached => ConnectivityState::Offline,
            ConnectivityState::Isolated => ConnectivityState::Offline,
            ConnectivityState::Commissioning => ConnectivityState::Offline,

            state => state.clone(),
        }
    }

    fn role_updated(&self, role: Role) -> ConnectivityState {
        match self {
            ConnectivityState::Attaching | ConnectivityState::Commissioning => match role {
                Role::Detached => self.clone(),
                _ => ConnectivityState::Attached,
            },

            ConnectivityState::Attached | ConnectivityState::Isolated => match role {
                Role::Detached => ConnectivityState::Isolated,
                _ => ConnectivityState::Attached,
            },

            state => state.clone(),
        }
    }
}

/// Enumeration describing the initialization state of the
/// driver.
///
/// See `doc/lowpan-init-state.svg` for a diagram.
#[derive(Debug, Eq, PartialEq, Copy, Clone)]
pub(super) enum InitState {
    /// The driver is completely uninitialized.
    Uninitialized,

    /// The initialization process is waiting for a reset to be
    /// acknowledged from the device.
    WaitingForReset,

    /// The initialization process is now recovering from the
    /// reset and synchronizing with the device.
    RecoverFromReset,

    /// The device has been fully initialized and is ready for use.
    Initialized,
}

impl Default for InitState {
    fn default() -> Self {
        Self::Uninitialized
    }
}

impl InitState {
    pub fn is_initialized(&self) -> bool {
        *self == InitState::Initialized
    }

    pub fn is_initializing(&self) -> bool {
        *self != InitState::Initialized
    }
}

impl<DS: SpinelDeviceClient, NI: NetworkInterface> SpinelDriver<DS, NI> {
    /// Asynchronous task that waits for the given `DriverState`
    /// snapshot predicate closure to return true.
    ///
    /// If the predicate returns true, the task ends immediately.
    /// If the predicate returns false, the task will sleep until
    /// the next driver state change, upon which the predicate will
    /// be checked again.
    pub(super) async fn wait_for_state<FN>(&self, predicate: FN)
    where
        FN: Fn(&DriverState) -> bool,
    {
        loop {
            {
                let driver_state = self.driver_state.lock();
                if predicate(driver_state.deref()) {
                    break;
                }
            }
            self.driver_state_change.wait().await;
        }
    }

    pub(super) fn get_connectivity_state(&self) -> ConnectivityState {
        self.driver_state.lock().connectivity_state
    }

    /// Called whenever the driver state has changed.
    pub(super) fn on_connectivity_state_change(
        &self,
        new_state: ConnectivityState,
        old_state: ConnectivityState,
    ) {
        fx_log_info!("SpinelDriver: State Change: {:?} -> {:?}", old_state, new_state);

        self.driver_state_change.trigger();

        match (old_state, new_state) {
            // TODO: Add state transition tasks here.

            // Unhandled state transition.
            (_, _) => {}
        }
    }
}
