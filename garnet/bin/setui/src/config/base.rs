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

#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    /// A load of a config file with the given information about the load.
    Load(ConfigLoadInfo),
}

#[derive(Clone, Debug, PartialEq)]
pub struct ConfigLoadInfo {
    /// The path at which the config was loaded.
    pub path: String,
    /// The status of the load.
    pub status: ConfigLoadStatus,
    /// The contents of the loaded config file.
    pub contents: Option<String>,
}

#[derive(Clone, Debug, PartialEq)]
pub enum ConfigLoadStatus {
    /// Failed to parse the file.
    ParseFailure(String),
    /// Successfully loaded the file.
    Success,
    /// Falling back to default.
    UsingDefaults(String),
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
    /// Responsible for recording internal state of messages sent on the message
    /// hub to policy proxies handlers.
    InspectPolicy,
    /// Responsible for logging all settings values of messages between the
    /// proxy and setting handlers to Inspect.
    InspectSettingData,
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
            AgentType::InspectPolicy => {
                storage_factory
                    .initialize::<crate::agent::inspect_policy::InspectPolicyAgent>()
                    .await
            }
            AgentType::InspectSettingData => {
                storage_factory
                    .initialize::<crate::agent::inspect_setting_data::InspectSettingAgent>()
                    .await
            }
        }
    }
}

pub fn get_default_agent_types() -> HashSet<AgentType> {
    [AgentType::Restore].iter().copied().collect()
}

impl From<AgentType> for BlueprintHandle {
    fn from(agent_type: AgentType) -> BlueprintHandle {
        match agent_type {
            AgentType::CameraWatcher => crate::agent::camera_watcher::blueprint::create(),
            AgentType::Earcons => crate::agent::earcons::agent::blueprint::create(),
            AgentType::MediaButtons => crate::agent::media_buttons::blueprint::create(),
            AgentType::Restore => crate::agent::restore_agent::blueprint::create(),
            AgentType::Inspect => crate::agent::inspect::blueprint::create(),
            AgentType::InspectPolicy => crate::agent::inspect_policy::blueprint::create(),
            AgentType::InspectSettingData => {
                crate::agent::inspect_setting_data::blueprint::create()
            }
        }
    }
}
