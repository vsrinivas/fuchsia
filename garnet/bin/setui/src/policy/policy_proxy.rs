// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async::Task;
use futures::StreamExt;

use crate::internal::core;
use crate::internal::policy;
use crate::internal::policy::Address;
use crate::message::base::{MessageEvent, MessengerType};
use crate::policy::base::Request;
use crate::policy::policy_handler::PolicyHandler;
use crate::switchboard::base::{SettingAction, SettingActionData, SettingRequest, SettingType};

/// `PolicyProxy` handles the routing of policy requests and the intercepting of setting requests to
/// a [`PolicyHandler`].
///
/// [`PolicyHandler`]: trait.PolicyHandler.html
// TODO(fxbug.dev/59747): remove when used
#[allow(dead_code)]
pub struct PolicyProxy {
    setting_type: SettingType,
    policy_handler: Box<dyn PolicyHandler + Send + Sync + 'static>,
    core_messenger: core::message::Messenger,
    policy_messenger: policy::message::Messenger,
}

impl PolicyProxy {
    pub async fn create(
        setting_type: SettingType,
        policy_handler: Box<dyn PolicyHandler + Send + Sync + 'static>,
        core_messenger_factory: core::message::Factory,
        policy_messenger_factory: policy::message::Factory,
    ) -> Result<(), Error> {
        // TODO(fxbug.dev/59747): filter so that we only get the setting requests we care about.
        let core_messenger_result =
            core_messenger_factory.create(MessengerType::Broker(None)).await;
        let (core_messenger, core_receptor) = core_messenger_result.map_err(Error::new)?;

        let policy_messenger_result = policy_messenger_factory
            .create(MessengerType::Addressable(Address::Policy(setting_type)))
            .await;
        let (policy_messenger, policy_receptor) = policy_messenger_result.map_err(Error::new)?;

        let mut proxy = Self { setting_type, policy_handler, core_messenger, policy_messenger };

        Task::spawn(async move {
            let policy_fuse = policy_receptor.fuse();
            let core_fuse = core_receptor.fuse();
            futures::pin_mut!(policy_fuse, core_fuse);
            loop {
                futures::select! {
                    // Handle policy messages.
                    policy_event = policy_fuse.select_next_some() => {
                        if let MessageEvent::Message(
                            policy::Payload::Request(request),
                            message_client,
                        ) = policy_event
                        {
                            proxy.process_policy_request(request, message_client).await;
                        }
                    }

                    // Handle intercepted messages from the core message hub.
                    core_event = core_fuse.select_next_some() => {
                        if let MessageEvent::Message(
                            core::Payload::Action(SettingAction {
                                data: SettingActionData::Request(request),
                                ..
                            }),
                            message_client,
                        ) = core_event
                        {
                            proxy.process_settings_request(request, message_client).await;
                        }
                    }

                    // This shouldn't ever be triggered since the policy proxy (and its receptors)
                    // should be active for the duration of the service. This is just a safeguard to
                    // ensure this detached task doesn't run forever if the receptors stop somehow.
                    complete => break,
                };
            }
        })
        .detach();

        Ok(())
    }

    async fn process_policy_request(
        &mut self,
        request: Request,
        message_client: policy::message::Client,
    ) {
        let response = self.policy_handler.handle_policy_request(request).await;
        message_client.reply(policy::Payload::Response(response)).send();
    }

    async fn process_settings_request(
        &self,
        _request: SettingRequest,
        _message_client: core::message::Client,
    ) {
        // TODO(fxbug.dev/59747): once we properly filtered just the requests we want, intercept
        // them and send them to the handler.
    }
}
