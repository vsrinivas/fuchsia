// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async::Task;
use futures::StreamExt;

use crate::base::SettingType;
use crate::internal::core;
use crate::internal::policy;
use crate::internal::policy::Address;
use crate::message::base::{filter, MessageEvent, MessengerType};
use crate::policy::base::{PolicyHandlerFactory, Request};
use crate::policy::policy_handler::{EventTransform, PolicyHandler, RequestTransform};
use crate::switchboard::base::{SettingAction, SettingActionData, SettingEvent, SettingRequest};
use futures::lock::Mutex;
use std::sync::Arc;

/// `PolicyProxy` handles the routing of policy requests and the intercepting of setting requests to
/// a [`PolicyHandler`].
///
/// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
pub struct PolicyProxy {
    policy_handler: Box<dyn PolicyHandler + Send + Sync + 'static>,
}

impl PolicyProxy {
    pub async fn create(
        setting_type: SettingType,
        handler_factory: Arc<Mutex<dyn PolicyHandlerFactory + Send + Sync>>,
        core_messenger_factory: core::message::Factory,
        policy_messenger_factory: policy::message::Factory,
        setting_proxy_signature: core::message::Signature,
    ) -> Result<core::message::Signature, Error> {
        let (_, switchboard_receptor) = core_messenger_factory
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
            .await
            .map_err(Error::new)?;

        let (_, proxy_receptor) = core_messenger_factory
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    // Only catch messages that were sent by the setting proxy and contain a
                    // SettingEvent.
                    message.get_author() == setting_proxy_signature
                        && matches!(message.payload(), core::Payload::Event(_))
                })),
            ))))
            .await
            .map_err(Error::new)?;

        let policy_messenger_result = policy_messenger_factory
            .create(MessengerType::Addressable(Address::Policy(setting_type)))
            .await;
        let (_, policy_receptor) = policy_messenger_result.map_err(Error::new)?;

        let (handler_messenger, handler_receptor) =
            core_messenger_factory.create(MessengerType::Unbound).await.map_err(Error::new)?;

        let policy_handler = handler_factory
            .lock()
            .await
            .generate(setting_type, handler_messenger, setting_proxy_signature)
            .await?;

        let mut proxy = Self { policy_handler };

        Task::spawn(async move {
            let policy_fuse = policy_receptor.fuse();
            let switchboard_fuse = switchboard_receptor.fuse();
            let proxy_fuse = proxy_receptor.fuse();
            futures::pin_mut!(policy_fuse, switchboard_fuse, proxy_fuse);
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

                    // Handle intercepted messages from the switchboard.
                    switchboard_message = switchboard_fuse.select_next_some() => {
                        if let MessageEvent::Message(
                            core::Payload::Action(SettingAction {
                                id,
                                setting_type,
                                data: SettingActionData::Request(request),
                            }),
                            message_client,
                        ) = switchboard_message
                        {
                            proxy
                                .process_settings_request(id, setting_type, request, message_client)
                                .await;
                        }
                    }

                    // Handler intercepted an event sent by the setting proxy.
                    proxy_message = proxy_fuse.select_next_some() => {
                        if let MessageEvent::Message(core::Payload::Event(setting_event), message_client) = proxy_message
                        {
                            proxy
                                .process_settings_event(setting_event, message_client)
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

        Ok(handler_receptor.get_signature())
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
    /// based on the [`RequestTransform`], such as ignoring the message, intercepting the message and
    /// answering the client directly, or forwarding the message with a modified request.
    ///
    /// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
    /// [`RequestTransform`]: ../policy_handler/enum.RequestTransform.html
    async fn process_settings_request(
        &mut self,
        request_id: u64,
        setting_type: SettingType,
        request: SettingRequest,
        message_client: core::message::Client,
    ) {
        let handler_result = self.policy_handler.handle_setting_request(request).await;
        match handler_result {
            Some(RequestTransform::Request(modified_request)) => {
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
            Some(RequestTransform::Result(result)) => {
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

    /// Passes the given setting event to the [`PolicyHandler`], then take an appropriate action
    /// based on the [`EventTransform`] it returns, such as ignoring the event or forwarding the
    /// event with a modified request.
    ///
    /// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
    /// [`EventTransform`]: ../policy_handler/enum.EventTransform.html
    async fn process_settings_event(
        &mut self,
        event: SettingEvent,
        message_client: core::message::Client,
    ) {
        let handler_result = self.policy_handler.handle_setting_event(event).await;
        match handler_result {
            Some(EventTransform::Event(setting_event)) => {
                // Handler provided a modified setting event to forward to the switchboard in place
                // of the original.
                message_client.propagate(core::Payload::Event(setting_event)).send();
            }
            // Don't do anything with the message, it'll continue onwards to the switchboard as
            // expected.
            None => return,
        }
    }
}
