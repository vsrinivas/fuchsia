// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async::Task;
use futures::StreamExt;

use crate::internal::core;
use crate::internal::policy;
use crate::internal::policy::Address;
use crate::message::base::{filter, MessageEvent, MessengerType};
use crate::policy::base::Request;
use crate::policy::policy_handler::{PolicyHandler, Transform};
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingType,
};
use std::sync::Arc;

/// `PolicyProxy` handles the routing of policy requests and the intercepting of setting requests to
/// a [`PolicyHandler`].
///
/// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
pub struct PolicyProxy {
    policy_handler: Box<dyn PolicyHandler + Send + Sync + 'static>,
    core_messenger: core::message::Messenger,
}

impl PolicyProxy {
    pub async fn create(
        setting_type: SettingType,
        policy_handler: Box<dyn PolicyHandler + Send + Sync + 'static>,
        core_messenger_factory: core::message::Factory,
        policy_messenger_factory: policy::message::Factory,
    ) -> Result<(), Error> {
        let core_messenger_result = core_messenger_factory
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    // Only catch messages that were originally sent from the switchboard, and that
                    // contain a request for the specific setting type we're interested in.
                    matches!(
                        message.get_author(),
                        core::message::Signature::Address(core::Address::Switchboard)
                    ) && matches!(
                        message.payload(),
                        core::Payload::Action(SettingAction {setting_type: s, ..})
                            if s == setting_type
                    )
                })),
            ))))
            .await;
        let (core_messenger, core_receptor) = core_messenger_result.map_err(Error::new)?;

        let policy_messenger_result = policy_messenger_factory
            .create(MessengerType::Addressable(Address::Policy(setting_type)))
            .await;
        let (_, policy_receptor) = policy_messenger_result.map_err(Error::new)?;

        let mut proxy = Self { policy_handler, core_messenger };

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
                                id,
                                setting_type,
                                data: SettingActionData::Request(request),
                            }),
                            message_client,
                        ) = core_event
                        {
                            proxy
                                .process_settings_request(id, setting_type, request, message_client)
                                .await;
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

    /// Passes the given setting request to the [`PolicyHandler`], then take an appropriate action
    /// based on the [`Transform`], such as ignoring the message, intercepting the message and
    /// answering the client directly, or forwarding the message with a modified request.
    ///
    /// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
    /// [`Transform`]: ../policy_handler/enum.Transform.html
    async fn process_settings_request(
        &mut self,
        request_id: u64,
        setting_type: SettingType,
        request: SettingRequest,
        message_client: core::message::Client,
    ) {
        let handler_result =
            self.policy_handler.handle_setting_request(request, self.core_messenger.clone()).await;
        match handler_result {
            Some(Transform::Request(modified_request)) => {
                // Handler provided a modified request to forward to the setting handler in place of
                // the original.
                message_client
                    .propagate(core::Payload::Action(SettingAction {
                        id: request_id,
                        setting_type,
                        data: SettingActionData::Request(modified_request),
                    }))
                    .send();
            }
            Some(Transform::Result(result)) => {
                // Handler provided a result to return directly to the client, respond to the
                // intercepted message with the result. By replying through the MessageClient, the
                // message doesn't continue to be propagated to the setting handler.
                message_client
                    .reply(core::Payload::Event(SettingEvent::Response(request_id, result)))
                    .send();
            }
            // Don't do anything with the message, it'll continue onwards to the handler as
            // expected.
            None => return,
        }
    }
}
