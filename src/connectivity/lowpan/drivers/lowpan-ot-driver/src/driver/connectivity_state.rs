// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude::*;
use fidl_fuchsia_lowpan::{ConnectivityState, Role};

/// Extension trait for adding some helper methods to
/// [`::fidl_fuchsia_lowpan::ConnectivityState`].
///
/// See `doc/lowpan-connectivity-state.svg` for a diagram.
pub trait ConnectivityStateExt {
    /// Indicates if the current state is considered 'ready' or not.
    fn is_ready(&self) -> bool;

    /// Indicates if the current state is considered 'active' or not.
    fn is_active(&self) -> bool;

    /// Indicates if the current state is considered simultaneously
    /// 'active' and 'ready' (could also be described as 'online')
    fn is_active_and_ready(&self) -> bool;

    /// Indicates if we are commissioning or not.
    fn is_commissioning(&self) -> bool;

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

    fn commissioning(&self) -> Result<ConnectivityState, anyhow::Error>;
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
            ConnectivityState::Commissioning => false,
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

    fn is_invalid_during_initialization(&self) -> bool {
        self.is_online()
    }

    fn is_commissioning(&self) -> bool {
        *self == ConnectivityState::Commissioning
    }

    fn is_online(&self) -> bool {
        match self {
            ConnectivityState::Inactive => false,
            ConnectivityState::Ready => false,
            ConnectivityState::Offline => false,
            ConnectivityState::Attaching => false,

            ConnectivityState::Attached => true,
            ConnectivityState::Isolated => true,
            ConnectivityState::Commissioning => false,
        }
    }

    fn activated(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Inactive => ConnectivityState::Offline,
            ConnectivityState::Ready => ConnectivityState::Attaching,

            state => *state,
        }
    }

    fn deactivated(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Offline => ConnectivityState::Inactive,
            ConnectivityState::Commissioning => ConnectivityState::Inactive,

            ConnectivityState::Attaching => ConnectivityState::Ready,
            ConnectivityState::Attached => ConnectivityState::Ready,
            ConnectivityState::Isolated => ConnectivityState::Ready,

            state => *state,
        }
    }

    fn provisioned(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Inactive => ConnectivityState::Ready,
            ConnectivityState::Offline => ConnectivityState::Attaching,
            ConnectivityState::Commissioning => ConnectivityState::Attaching,

            state => *state,
        }
    }

    fn unprovisioned(&self) -> ConnectivityState {
        match self {
            ConnectivityState::Ready => ConnectivityState::Inactive,

            ConnectivityState::Attaching => ConnectivityState::Offline,
            ConnectivityState::Attached => ConnectivityState::Offline,
            ConnectivityState::Isolated => ConnectivityState::Offline,
            ConnectivityState::Commissioning => ConnectivityState::Offline,

            state => *state,
        }
    }

    fn role_updated(&self, role: Role) -> ConnectivityState {
        match self {
            ConnectivityState::Attaching | ConnectivityState::Commissioning => match role {
                Role::Detached => *self,
                _ => ConnectivityState::Attached,
            },

            ConnectivityState::Attached | ConnectivityState::Isolated => match role {
                Role::Detached => ConnectivityState::Isolated,
                _ => ConnectivityState::Attached,
            },

            state => *state,
        }
    }

    fn commissioning(&self) -> Result<ConnectivityState, anyhow::Error> {
        if self.is_active() {
            Ok(ConnectivityState::Commissioning)
        } else {
            Err(format_err!(
                "Can't transition to ConnectivityState::Commissioning from {:?}",
                *self,
            ))
        }
    }
}
