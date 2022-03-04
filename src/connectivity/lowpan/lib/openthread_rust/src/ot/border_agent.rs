// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;
use num::FromPrimitive;

/// Represents the thread joiner state.
///
/// Functional equivalent of [`otsys::otJoinerState`](crate::otsys::otJoinerState).
#[derive(
    Debug,
    Copy,
    Clone,
    Eq,
    Ord,
    PartialOrd,
    PartialEq,
    num_derive::FromPrimitive,
    num_derive::ToPrimitive,
)]
pub enum BorderAgentState {
    /// Functional equivalent of [`otsys::OT_BORDER_AGENT_STATE_STOPPED`](crate::otsys::OT_BORDER_AGENT_STATE_STOPPED).
    Stopped = OT_BORDER_AGENT_STATE_STOPPED as isize,

    /// Functional equivalent of [`otsys::OT_BORDER_AGENT_STATE_STARTED`](crate::otsys::OT_BORDER_AGENT_STATE_STARTED).
    Started = OT_BORDER_AGENT_STATE_STARTED as isize,

    /// Functional equivalent of [`otsys::OT_BORDER_AGENT_STATE_ACTIVE`](crate::otsys::OT_BORDER_AGENT_STATE_ACTIVE).
    Active = OT_BORDER_AGENT_STATE_ACTIVE as isize,
}

impl From<otBorderAgentState> for BorderAgentState {
    fn from(x: otBorderAgentState) -> Self {
        Self::from_u32(x).unwrap_or_else(|| panic!("Unknown otBorderAgentState value: {}", x))
    }
}

impl From<BorderAgentState> for otBorderAgentState {
    fn from(x: BorderAgentState) -> Self {
        x as otBorderAgentState
    }
}

/// Methods from the [OpenThread "Border Agent" Module][1].
///
/// [1]: https://openthread.io/reference/group/api-border-agent
pub trait BorderAgent {
    /// Functional equivalent of
    /// [`otsys::otBorderAgentGetState`](crate::otsys::otBorderAgentGetState).
    fn border_agent_get_state(&self) -> BorderAgentState;

    /// Functional equivalent of
    /// [`otsys::otBorderAgentUdpPort`](crate::otsys::otBorderAgentGetUdpPort).
    fn border_agent_get_udp_port(&self) -> u16;
}

impl<T: BorderAgent + Boxable> BorderAgent for ot::Box<T> {
    fn border_agent_get_state(&self) -> BorderAgentState {
        self.as_ref().border_agent_get_state()
    }

    fn border_agent_get_udp_port(&self) -> u16 {
        self.as_ref().border_agent_get_udp_port()
    }
}

impl BorderAgent for Instance {
    fn border_agent_get_state(&self) -> BorderAgentState {
        unsafe { otBorderAgentGetState(self.as_ot_ptr()) }.into()
    }

    fn border_agent_get_udp_port(&self) -> u16 {
        unsafe { otBorderAgentGetUdpPort(self.as_ot_ptr()) }
    }
}
