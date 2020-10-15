// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::audio::policy::{PolicyId, Request, Response, State, StateBuilder};
use crate::internal::core::message::Messenger;
use crate::policy::base as policy_base;
use crate::policy::policy_handler::{PolicyHandler, Transform};
use crate::switchboard::base::SettingRequest;
use async_trait::async_trait;

/// `AudioPolicyHandler` controls the persistence and enforcement of audio policies set by
/// fuchsia.settings.policy.VolumePolicyController.
pub struct AudioPolicyHandler {
    state: State,
}

impl AudioPolicyHandler {
    pub fn create() -> Self {
        Self { state: StateBuilder::new().build() }
    }
}

#[async_trait]
impl PolicyHandler for AudioPolicyHandler {
    async fn handle_policy_request(
        &mut self,
        request: policy_base::Request,
    ) -> policy_base::response::Response {
        // TODO(fxbug.dev/60966): replace with real implementation of Get/Add/Remove
        Ok(policy_base::response::Payload::Audio(match request {
            policy_base::Request::Audio(audio_request) => match audio_request {
                Request::Get => Response::State(self.state.clone()),
                Request::AddPolicy(_, _) => Response::Policy(PolicyId(0)),
                Request::RemovePolicy(_) => Response::Policy(PolicyId(0)),
            },
        }))
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
