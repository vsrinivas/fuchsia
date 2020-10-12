// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::BlueprintHandle;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;

/// The flags used to control behavior of controllers.
#[derive(PartialEq, Debug, Eq, Hash, Copy, Clone, Serialize, Deserialize)]
pub enum ControllerFlag {
    /// This flag controls whether an external service is in control of the
    /// brightness configuration.
    ExternalBrightnessControl,
}

/// Represents each agent that can be run.
#[derive(Eq, PartialEq, Hash, Debug, Copy, Clone, Deserialize)]
pub enum AgentType {
    /// Plays earcons in response to certain events. If MediaButtons is
    /// enabled, then it will also handle some media buttons events.
    Earcons,
    /// Responsible for managing the connection to media buttons. It will
    /// broadcast events to the controllers and agents.
    MediaButtons,
    /// Responsible for initializing all of the controllers.
    Restore,
}

pub fn get_default_agent_types() -> HashSet<AgentType> {
    return vec![AgentType::Restore].into_iter().collect();
}

impl From<AgentType> for Option<BlueprintHandle> {
    fn from(agent_type: AgentType) -> Option<BlueprintHandle> {
        match agent_type {
            AgentType::Earcons => Some(crate::agent::earcons::agent::blueprint::create()),
            // TODO(fxb/57919): Enable when media buttons agent is fully implemented.
            AgentType::MediaButtons => None,
            AgentType::Restore => Some(crate::agent::restore_agent::blueprint::create()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // Validates that MediaButtonsAgent is the only agent that maps to no agent creation when
    // using the default agent mapping.
    #[test]
    fn test_default_agent_mapping() {
        assert!(
            <Option<BlueprintHandle> as From<AgentType>>::from(AgentType::Earcons).is_some(),
            "Earcons agent should be enabled"
        );
        assert!(
            <Option<BlueprintHandle> as From<AgentType>>::from(AgentType::MediaButtons).is_none(),
            "MediaButtons agent should be disabled"
        );
        assert!(
            <Option<BlueprintHandle> as From<AgentType>>::from(AgentType::Restore).is_some(),
            "Restore agent should be enabled"
        );
    }
}
