// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::BlueprintHandle;
use crate::handler::device_storage::DeviceStorageFactory;
use anyhow::Error;
use serde::{Deserialize, Serialize};
use std::collections::HashSet;
use std::sync::Arc;

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
    /// Responsible for watching the camera3 mute status. If other clients
    /// of the camera3 api modify the camera state, the agent should watch and
    /// should coordinate that change with the internal camera state.
    CameraWatcher,
    /// Plays earcons in response to certain events. If MediaButtons is
    /// enabled, then it will also handle some media buttons events.
    Earcons,
    /// Responsible for managing the connection to media buttons. It will
    /// broadcast events to the controllers and agents.
    MediaButtons,
    /// Responsible for initializing all of the controllers.
    Restore,
    /// Responsible for logging to Inspect.
    Inspect,
}

impl AgentType {
    /// Return the storage keys needed for this particular agent.
    pub async fn initialize_storage<T>(&self, storage_factory: &Arc<T>) -> Result<(), Error>
    where
        T: DeviceStorageFactory,
    {
        match self {
            AgentType::CameraWatcher => {
                storage_factory
                    .initialize::<crate::agent::camera_watcher::CameraWatcherAgent>()
                    .await
            }
            AgentType::Earcons => {
                storage_factory.initialize::<crate::agent::earcons::agent::Agent>().await
            }
            AgentType::MediaButtons => {
                storage_factory.initialize::<crate::agent::media_buttons::MediaButtonsAgent>().await
            }
            AgentType::Restore => {
                storage_factory.initialize::<crate::agent::restore_agent::RestoreAgent>().await
            }
            AgentType::Inspect => {
                storage_factory.initialize::<crate::agent::inspect::InspectAgent>().await
            }
        }
    }
}

pub fn get_default_agent_types() -> HashSet<AgentType> {
    return vec![AgentType::Restore].into_iter().collect();
}

impl From<AgentType> for BlueprintHandle {
    fn from(agent_type: AgentType) -> BlueprintHandle {
        match agent_type {
            AgentType::CameraWatcher => crate::agent::camera_watcher::blueprint::create(),
            AgentType::Earcons => crate::agent::earcons::agent::blueprint::create(),
            AgentType::MediaButtons => crate::agent::media_buttons::blueprint::create(),
            AgentType::Restore => crate::agent::restore_agent::blueprint::create(),
            AgentType::Inspect => crate::agent::inspect::blueprint::create(),
        }
    }
}
