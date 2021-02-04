// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async::Task;
use futures::StreamExt;

use crate::base::SettingType;
use crate::handler::base::{Error as HandlerError, Payload, Request, Response};
use crate::internal::core;
use crate::internal::policy;
use crate::internal::policy::{Address, Role};
use crate::message::base::{filter, role, MessageEvent, MessengerType};
use crate::policy::base::{PolicyHandlerFactory, PolicyType, Request as PolicyRequest};
use crate::policy::policy_handler::{
    EventTransform, PolicyHandler, RequestTransform, ResponseTransform,
};
use crate::service;
use crate::service::TryFromWithClient;
use crate::switchboard::base::{SettingAction, SettingActionData, SettingEvent};
use futures::lock::Mutex;
use std::convert::TryFrom;
use std::sync::Arc;

/// `PolicyProxy` handles the routing of policy requests and the intercepting of setting requests to
/// a [`PolicyHandler`].
///
/// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
pub struct PolicyProxy {
    policy_handler: Box<dyn PolicyHandler + Send + Sync + 'static>,
}

impl PolicyProxy {
    /// Creates a policy proxy and returns the signatures it uses to communicate in the core message
    /// hub.
    pub async fn create(
        policy_type: PolicyType,
        handler_factory: Arc<Mutex<dyn PolicyHandlerFactory + Send + Sync>>,
        messenger_factory: service::message::Factory,
        core_messenger_factory: core::message::Factory,
        policy_messenger_factory: policy::message::Factory,
        setting_proxy_signature: core::message::Signature,
    ) -> Result<core::message::Signature, Error> {
        let setting_type = policy_type.setting_type();
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

        let setting_handler_address = service::Address::Handler(setting_type);

        // The policy proxy should intercept responses authored by the
        // setting proxy
        let response_author_filter = filter::Builder::new(
            filter::Condition::Author(service::message::Signature::Address(
                setting_handler_address,
            )),
            filter::Conjugation::All,
        )
        .append(filter::Condition::Custom(Arc::new(|message| {
            Payload::try_from(message.payload())
                .map_or(false, |payload| matches!(payload, Payload::Response(Ok(Some(_)))))
        })))
        .build();

        // The policy proxy should intercept all messages where the setting
        // proxy is the audience
        let request_audience_filter = filter::Builder::single(filter::Condition::Audience(
            service::message::Audience::Address(setting_handler_address),
        ));

        let service_proxy_filter = filter::Builder::new(
            filter::Condition::Filter(response_author_filter),
            filter::Conjugation::Any,
        )
        .append(filter::Condition::Filter(request_audience_filter))
        .build();

        let (_, service_proxy_receptor) = messenger_factory
            .create(MessengerType::Broker(Some(service_proxy_filter)))
            .await
            .map_err(Error::new)?;

        let (_, proxy_receptor) = core_messenger_factory
            .create(MessengerType::Broker(Some(filter::Builder::single(
                filter::Condition::Custom(Arc::new(move |message| {
                    // Only catch messages that were sent by the setting proxy and contain a
                    // SettingEvent. We check the message type to make sure we only capture event
                    // messages that originate from the proxy and not ones sent as replies.
                    message.get_author() == setting_proxy_signature
                        && matches!(message.payload(), core::Payload::Event(_))
                        && matches!(message.get_type(), core::message::MessageType::Origin(_))
                })),
            ))))
            .await
            .map_err(Error::new)?;

        let (_, policy_receptor) = policy_messenger_factory
            .messenger_builder(MessengerType::Addressable(Address::Policy(policy_type)))
            .add_role(role::Signature::role(Role::PolicyHandler))
            .build()
            .await
            .map_err(Error::new)?;

        let (handler_messenger, handler_receptor) =
            core_messenger_factory.create(MessengerType::Unbound).await.map_err(Error::new)?;

        let policy_handler = handler_factory
            .lock()
            .await
            .generate(policy_type, handler_messenger, setting_proxy_signature)
            .await?;

        let mut proxy = Self { policy_handler };

        Task::spawn(async move {
            let policy_fuse = policy_receptor.fuse();
            let switchboard_fuse = switchboard_receptor.fuse();
            let proxy_fuse = proxy_receptor.fuse();
            let message_fuse = service_proxy_receptor.fuse();
            futures::pin_mut!(policy_fuse, switchboard_fuse, proxy_fuse, message_fuse);
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

                    // Handle intercepted messages from the service MessageHub
                    message = message_fuse.select_next_some() => {
                        proxy.process_settings_event(message).await;
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
                                .process_settings_request_switchboard(id, setting_type, request,
                                    message_client)
                                .await;
                        }
                    }

                    // Handler intercepted an event sent by the setting proxy.
                    proxy_message = proxy_fuse.select_next_some() => {
                        if let MessageEvent::Message(core::Payload::Event(setting_event), message_client) = proxy_message
                        {
                            proxy
                                .process_settings_event_switchboard(setting_event, message_client)
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
        request: PolicyRequest,
        message_client: policy::message::MessageClient,
    ) {
        let response = self.policy_handler.handle_policy_request(request).await;
        message_client.reply(policy::Payload::Response(response)).send();
    }

    async fn process_settings_event(&mut self, event: service::message::MessageEvent) {
        if let Ok((payload, client)) = Payload::try_from_with_client(event) {
            match payload {
                Payload::Request(request) => {
                    self.process_settings_request(request, client).await;
                }
                Payload::Response(response) => {
                    self.process_settings_response(response, client).await;
                }
            }
        }
    }

    /// Passes the given setting request to the [`PolicyHandler`], then take an appropriate action
    /// based on the [`RequestTransform`], such as ignoring the message, intercepting the message and
    /// answering the client directly, or forwarding the message with a modified request.
    ///
    /// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
    /// [`RequestTransform`]: ../policy_handler/enum.RequestTransform.html
    async fn process_settings_request(
        &mut self,
        request: Request,
        message_client: service::message::MessageClient,
    ) {
        let handler_result = self.policy_handler.handle_setting_request(request).await;
        match handler_result {
            Some(RequestTransform::Request(modified_request)) => {
                message_client.propagate(Payload::Request(modified_request).into()).send();
            }
            Some(RequestTransform::Result(result)) => {
                // Handler provided a result to return directly to the client, respond to the
                // intercepted message with the result. By replying through the MessageClient, the
                // message doesn't continue to be propagated to the setting handler.
                message_client
                    .reply(Payload::Response(result.map_err(HandlerError::from)).into())
                    .send();
            }
            // Don't do anything with the message, it'll continue onwards to the handler as
            // expected.
            None => return,
        }
    }

    /// Passes the given setting response to the [`PolicyHandler`], then take an appropriate action
    /// based on the [`ResponseTransform`] it returns, such as ignoring the response or forwarding
    /// the event with a modified request.
    ///
    /// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
    /// [`ResponseTransform`]: ../policy_handler/enum.ResponseTransform.html
    async fn process_settings_response(
        &mut self,
        response: Response,
        client: service::message::MessageClient,
    ) {
        let handler_result = self.policy_handler.handle_setting_response(response).await;
        if let Some(ResponseTransform::Response(response)) = handler_result {
            // Handler provided a modified setting event to forward to the switchboard in place
            // of the original.
            client.propagate(Payload::Response(response).into()).send();
        }
    }

    /// Passes the given setting request to the [`PolicyHandler`], then take an appropriate action
    /// based on the [`RequestTransform`], such as ignoring the message, intercepting the message
    /// and answering the client directly, or forwarding the message with a modified request.
    ///
    /// [`PolicyHandler`]: ../policy_handler/trait.PolicyHandler.html
    /// [`RequestTransform`]: ../policy_handler/enum.RequestTransform.html
    async fn process_settings_request_switchboard(
        &mut self,
        request_id: u64,
        setting_type: SettingType,
        request: Request,
        message_client: core::message::MessageClient,
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
    async fn process_settings_event_switchboard(
        &mut self,
        event: SettingEvent,
        message_client: core::message::MessageClient,
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
