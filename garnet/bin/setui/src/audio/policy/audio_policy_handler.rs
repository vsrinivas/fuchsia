// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::default_audio_info;
use crate::audio::policy::{Request, Response, State, StateBuilder, TransformFlags};
use crate::internal::core::message::Messenger;
use crate::policy::base as policy_base;
use crate::policy::base::response::Error as PolicyError;
use crate::policy::policy_handler::{ClientProxy, Create, PolicyHandler, Transform};
use crate::switchboard::base::SettingRequest;
use anyhow::Error;
use async_trait::async_trait;
use std::collections::hash_map::Entry;

/// Used as the argument field in a ControllerError::InvalidArgument to signal the FIDL handler to
/// signal that the policy ID was invalid.
pub const ARG_POLICY_ID: &'static str = "policy_id";

/// `AudioPolicyHandler` controls the persistence and enforcement of audio policies set by
/// fuchsia.settings.policy.VolumePolicyController.
pub struct AudioPolicyHandler {
    state: State,
    client_proxy: ClientProxy<State>,
}

#[async_trait]
impl Create<State> for AudioPolicyHandler {
    async fn create(client_proxy: ClientProxy<State>) -> Result<Self, Error> {
        Ok(Self {
            state: AudioPolicyHandler::restore_policy_state(client_proxy.read().await),
            client_proxy,
        })
    }
}

#[async_trait]
impl PolicyHandler for AudioPolicyHandler {
    async fn handle_policy_request(
        &mut self,
        request: policy_base::Request,
    ) -> policy_base::response::Response {
        let audio_response = match request {
            policy_base::Request::Audio(audio_request) => match audio_request {
                Request::Get => Response::State(self.state.clone()),
                Request::AddPolicy(target, transform) => {
                    // TODO(fxbug.dev/60966): perform validations and return errors for invalid
                    // inputs
                    match self.state.properties.entry(target) {
                        Entry::Occupied(mut property) => {
                            let policy_id = property.get_mut().add_transform(transform);
                            self.client_proxy.write(self.state.clone(), false).await?;
                            Response::Policy(policy_id)
                        }
                        // TODO(fxbug.dev/60925): once policy targets are configurable, test this
                        // error case.
                        Entry::Vacant(_) => {
                            return Err(PolicyError::InvalidArgument(
                                self.client_proxy.setting_type(),
                                "target".into(),
                                format!("{:?}", target).into(),
                            ));
                        }
                    }
                }
                Request::RemovePolicy(policy_id) => {
                    match self.state.remove_policy(policy_id) {
                        Some(_) => {
                            // Found and removed a policy.
                            self.client_proxy.write(self.state.clone(), false).await?;
                            Response::Policy(policy_id)
                        }
                        None => {
                            // Policy not found.
                            return Err(PolicyError::InvalidArgument(
                                self.client_proxy.setting_type(),
                                ARG_POLICY_ID.into(),
                                format!("{:?}", policy_id).into(),
                            ));
                        }
                    }
                }
            },
        };

        Ok(policy_base::response::Payload::Audio(audio_response))
    }

    async fn handle_setting_request(
        &mut self,
        _request: SettingRequest,
        _messenger: Messenger,
    ) -> Option<Transform> {
        // TODO(fxbug.dev/60367): implement policy transforms
        return None;
    }
}

impl AudioPolicyHandler {
    /// Restores the policy state based on the configured audio streams and the previously persisted
    /// state.
    fn restore_policy_state(persisted_state: State) -> State {
        // Read the audio default info to see what policy targets are valid and create properties.
        let audio_info = default_audio_info();
        let mut state_builder = StateBuilder::new();
        for stream in audio_info.streams.iter() {
            // TODO(fxbug.dev/60925): read configuration to see what transform flags to enable for a
            // given stream.
            state_builder = state_builder.add_property(stream.stream_type, TransformFlags::all());
        }

        let mut state = state_builder.build();

        // Restore active policies from the persisted properties.
        for (target, persisted_property) in persisted_state.properties.into_iter() {
            state.properties.entry(target).and_modify(|property| {
                property.active_policies = persisted_property.active_policies
            });
        }

        state
    }
}
