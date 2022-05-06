// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async::Task;
use futures::StreamExt;

use crate::handler::base::{Error as HandlerError, Payload, Request, Response};
use crate::message::base::{filter, role, MessageEvent, MessageType, MessengerType};
use crate::policy::policy_handler::{PolicyHandler, RequestTransform, ResponseTransform};
use crate::policy::{
    self as policy_base, PolicyHandlerFactory, PolicyType, Request as PolicyRequest, Role,
};
use crate::service::TryFromWithClient;
use crate::{service, trace};
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
    /// Creates a policy proxy and returns the signatures it uses to communicate in the core message
    /// hub.
    pub(crate) async fn create(
        policy_type: PolicyType,
        handler_factory: Arc<Mutex<dyn PolicyHandlerFactory + Send + Sync>>,
        delegate: service::message::Delegate,
    ) -> Result<(), Error> {
        let (handler_messenger, receptor) =
            delegate.create(MessengerType::Unbound).await.map_err(Error::new)?;

        let setting_type = policy_type.setting_type();

        let setting_handler_address = service::Address::Handler(setting_type);

        // TODO(fxbug.dev/70948): investigate using scopes to eliminate the handler activity from
        // the observed messages.
        let policy_handler_signature = receptor.get_signature();
        // The policy proxy should intercept responses authored by the
        // setting proxy
        let response_author_filter = filter::Builder::new(
            filter::Condition::Author(service::message::Signature::Address(
                setting_handler_address,
            )),
            filter::Conjugation::All,
        )
        .append(filter::Condition::Custom(Arc::new(|message| {
            matches!(message.payload(), service::Payload::Setting(Payload::Response(Ok(Some(_)))))
        })))
        .append(filter::Condition::Custom(Arc::new(move |message| {
            if let MessageType::Reply(message) = message.get_type() {
                message.get_author() != policy_handler_signature
            } else {
                true
            }
        })))
        .build();

        // The policy proxy should intercept all messages where the setting
        // proxy is the audience
        let request_audience_filter = filter::Builder::new(
            filter::Condition::Audience(service::message::Audience::Address(
                setting_handler_address,
            )),
            filter::Conjugation::All,
        )
        .append(filter::Condition::Custom(Arc::new({
            let policy_handler_signature = receptor.get_signature();
            move |message| message.get_author() != policy_handler_signature
        })))
        .build();

        let service_proxy_filter = filter::Builder::new(
            filter::Condition::Filter(response_author_filter),
            filter::Conjugation::Any,
        )
        .append(filter::Condition::Filter(request_audience_filter))
        .build();

        let (_, service_proxy_receptor) =
            delegate.create(MessengerType::Broker(Some(service_proxy_filter))).await?;

        let (_, service_policy_receptor) = delegate
            .messenger_builder(MessengerType::Addressable(service::Address::PolicyHandler(
                policy_type,
            )))
            .add_role(role::Signature::role(service::Role::Policy(Role::PolicyHandler)))
            .build()
            .await?;

        let policy_handler =
            handler_factory.lock().await.generate(policy_type, handler_messenger).await?;

        let mut proxy = Self { policy_handler };

        Task::spawn(async move {
            let nonce = fuchsia_trace::generate_nonce();
            trace!(nonce, "policy proxy");
            let service_policy_fuse = service_policy_receptor.fuse();
            let message_fuse = service_proxy_receptor.fuse();
            futures::pin_mut!(message_fuse, service_policy_fuse);
            loop {
                futures::select! {
                    // Handle policy messages.
                    service_policy_event = service_policy_fuse.select_next_some() => {
                        trace!(
                            nonce,

                            "service policy event"
                        );
                        if let MessageEvent::Message(
                            service::Payload::Policy(policy_base::Payload::Request(request)),
                            message_client,
                        ) = service_policy_event
                        {
                            proxy.process_service_policy_request(request, message_client).await;
                        }
                    }

                    // Handle intercepted messages from the service MessageHub
                    message = message_fuse.select_next_some() => {
                        trace!(
                            nonce,

                            "message event"
                        );
                        proxy.process_settings_event(message).await;
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

    async fn process_service_policy_request(
        &mut self,
        request: PolicyRequest,
        message_client: service::message::MessageClient,
    ) {
        let response = self.policy_handler.handle_policy_request(request).await;
        // Ignore the receptor result.
        let _ = message_client
            .reply(service::Payload::Policy(policy_base::Payload::Response(response)))
            .send();
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
                // Ignore the receptor result.
                let _ = message_client.propagate(Payload::Request(modified_request).into()).send();
            }
            Some(RequestTransform::Result(result)) => {
                // Handler provided a result to return directly to the client, respond to the
                // intercepted message with the result. By replying through the MessageClient, the
                // message doesn't continue to be propagated to the setting handler.
                // Ignore the receptor result.
                let _ = message_client
                    .reply(Payload::Response(result.map_err(HandlerError::from)).into())
                    .send();
            }
            // Don't do anything with the message, it'll continue onwards to the handler as
            // expected.
            None => {}
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
            // Handler provided a modified setting event to forward to the requestor in place
            // of the original. Ignore the receptor result.
            let _ = client.propagate(Payload::Response(response).into()).send();
        }
    }
}
