// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::Payload;
use crate::agent::{AgentError, Context as AgentContext, Invocation, InvocationResult, Lifespan};
use crate::base::SettingType;
use crate::blueprint_definition;
use crate::event::{camera_watcher, Event, Publisher};
use crate::handler::base::{Payload as HandlerPayload, Request};
use crate::handler::device_storage::DeviceStorageAccess;
use crate::input::common::connect_to_camera;
use crate::message::base::Audience;
use crate::service_context::ServiceContext;
use crate::{service, trace, trace_guard};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use std::collections::HashSet;
use std::sync::Arc;

blueprint_definition!("camera_watcher_agent", CameraWatcherAgent::create);

/// Setting types that the camera watcher agent will send updates to, if they're
/// available on the device.
fn get_event_setting_types() -> HashSet<SettingType> {
    vec![SettingType::Input].into_iter().collect()
}

// TODO(fxbug.dev/70195): Extract common template from agents.
pub(crate) struct CameraWatcherAgent {
    publisher: Publisher,
    messenger: service::message::Messenger,

    /// Settings to send camera watcher events to.
    recipient_settings: HashSet<SettingType>,
}

impl DeviceStorageAccess for CameraWatcherAgent {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

impl CameraWatcherAgent {
    pub(crate) async fn create(context: AgentContext) {
        let mut agent = CameraWatcherAgent {
            publisher: context.get_publisher(),
            messenger: context
                .create_messenger()
                .await
                .expect("messenger should be created for CameraWatchAgent"),
            recipient_settings: context
                .available_components
                .intersection(&get_event_setting_types())
                .cloned()
                .collect::<HashSet<SettingType>>(),
        };

        let mut receptor = context.receptor;
        fasync::Task::spawn(async move {
            let nonce = fuchsia_trace::generate_nonce();
            let guard = trace_guard!(nonce, "camera watcher agent");
            while let Ok((payload, client)) = receptor.next_of::<Payload>().await {
                trace!(nonce, "payload");
                if let Payload::Invocation(invocation) = payload {
                    client
                        .reply(Payload::Complete(agent.handle(invocation).await).into())
                        .send()
                        .ack();
                }
            }
            drop(guard);

            fx_log_info!("Camera watcher agent done processing requests");
        })
        .detach()
    }

    async fn handle(&mut self, invocation: Invocation) -> InvocationResult {
        match invocation.lifespan {
            Lifespan::Initialization => Err(AgentError::UnhandledLifespan),
            Lifespan::Service => self.handle_service_lifespan(invocation.service_context).await,
        }
    }

    async fn handle_service_lifespan(
        &mut self,
        service_context: Arc<ServiceContext>,
    ) -> InvocationResult {
        match connect_to_camera(service_context).await {
            Ok(camera_device_client) => {
                let mut event_handler = EventHandler {
                    publisher: self.publisher.clone(),
                    messenger: self.messenger.clone(),
                    recipient_settings: self.recipient_settings.clone(),
                    sw_muted: false,
                };
                fasync::Task::spawn(async move {
                    let nonce = fuchsia_trace::generate_nonce();
                    // Here we don't care about hw_muted state because the input service would pick
                    // up mute changes directly from the switch. We care about sw changes because
                    // other clients of the camera3 service could change the sw mute state but not
                    // notify the settings service.
                    trace!(nonce, "camera_watcher_agent_handler");
                    while let Ok((sw_muted, _hw_muted)) =
                        camera_device_client.watch_mute_state().await
                    {
                        trace!(nonce, "event");
                        event_handler.handle_event(sw_muted);
                    }
                })
                .detach();

                Ok(())
            }
            Err(e) => {
                fx_log_err!("Unable to watch camera device: {:?}", e);
                Err(AgentError::UnexpectedError)
            }
        }
    }
}

struct EventHandler {
    publisher: Publisher,
    messenger: service::message::Messenger,
    recipient_settings: HashSet<SettingType>,
    sw_muted: bool,
}

impl EventHandler {
    fn handle_event(&mut self, sw_muted: bool) {
        if self.sw_muted != sw_muted {
            self.sw_muted = sw_muted;
            self.send_event(sw_muted);
        }
    }

    fn send_event(&self, muted: bool) {
        self.publisher.send_event(Event::CameraUpdate(camera_watcher::Event::OnSWMuteState(muted)));
        let setting_request: Request = Request::OnCameraSWState(muted);

        // Send the event to all the interested setting types that are also available.
        for setting_type in self.recipient_settings.iter() {
            self.messenger
                .message(
                    HandlerPayload::Request(setting_request.clone()).into(),
                    Audience::Address(service::Address::Handler(*setting_type)),
                )
                .send();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::event;
    use crate::message::base::{MessageEvent, MessengerType};
    use crate::message::receptor::Receptor;
    use crate::message::MessageHubUtil;
    use crate::service::{Address, Payload, Role};
    use crate::service_context::ServiceContext;
    use crate::tests::fakes::service_registry::ServiceRegistry;
    use crate::tests::helpers::{
        create_messenger_and_publisher, create_messenger_and_publisher_from_hub,
        create_receptor_for_setting_type,
    };
    use futures::StreamExt;
    use matches::assert_matches;

    // Tests that the initialization lifespan is not handled.
    #[fuchsia_async::run_until_stalled(test)]
    async fn initialization_lifespan_is_unhandled() {
        // Setup messengers needed to construct the agent.
        let (messenger, publisher) = create_messenger_and_publisher().await;

        // Construct the agent.
        let mut agent =
            CameraWatcherAgent { publisher, messenger, recipient_settings: HashSet::new() };

        // Try to initiatate the initialization lifespan.
        let result = agent
            .handle(Invocation {
                lifespan: Lifespan::Initialization,
                service_context: Arc::new(ServiceContext::new(None, None)),
            })
            .await;

        assert!(matches!(result, Err(AgentError::UnhandledLifespan)));
    }

    // Tests that the agent cannot start without a camera3 service.
    #[fuchsia_async::run_until_stalled(test)]
    async fn when_camera3_inaccessible_returns_err() {
        // Setup messengers needed to construct the agent.
        let (messenger, publisher) = create_messenger_and_publisher().await;

        // Construct the agent.
        let mut agent =
            CameraWatcherAgent { publisher, messenger, recipient_settings: HashSet::new() };

        let service_context = Arc::new(ServiceContext::new(
            // Create a service registry without a camera3 service interface.
            Some(ServiceRegistry::serve(ServiceRegistry::create())),
            None,
        ));

        // Try to initiate the Service lifespan without providing the camera3 fidl interface.
        let result =
            agent.handle(Invocation { lifespan: Lifespan::Service, service_context }).await;
        assert!(matches!(result, Err(AgentError::UnexpectedError)));
    }

    // Tests that events can be sent to the intended recipients.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_proxies_event() {
        let service_message_hub = service::MessageHub::create_hub();
        let (messenger, publisher) =
            create_messenger_and_publisher_from_hub(&service_message_hub).await;

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let event_receptor = service::build_event_listener(&service_message_hub).await;

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let handler_receptor: Receptor<Payload, Address, Role> =
            create_receptor_for_setting_type(&service_message_hub, SettingType::Unknown).await;

        let mut event_handler = EventHandler {
            publisher,
            messenger,
            recipient_settings: vec![SettingType::Unknown].into_iter().collect(),
            sw_muted: false,
        };

        // Send the events.
        event_handler.handle_event(true);

        // Delete the messengers for the receptors we're selecting below. This
        // will allow the `select!` to eventually hit the `complete` case.
        service_message_hub.delete(handler_receptor.get_signature());
        service_message_hub.delete(event_receptor.get_signature());

        let mut agent_received_sw_mute = false;
        let mut handler_received_event = false;

        let fused_event = event_receptor.fuse();
        let fused_setting_handler = handler_receptor.fuse();
        futures::pin_mut!(fused_event, fused_setting_handler);

        // Loop over the select so we can handle the messages as they come in. When all messages
        // have been handled, due to the messengers being deleted above, the complete branch should
        // be hit to break out of the loop.
        loop {
            futures::select! {
                message = fused_event.select_next_some() => {
                    if let MessageEvent::Message(service::Payload::Event(event::Payload::Event(
                        event::Event::CameraUpdate(event)
                    )), _) = message
                    {
                        match event {
                            event::camera_watcher::Event::OnSWMuteState(muted) => {
                                assert!(muted);
                                agent_received_sw_mute = true;
                            }
                        }
                    }
                },
                message = fused_setting_handler.select_next_some() => {
                    if let MessageEvent::Message(
                        service::Payload::Setting(HandlerPayload::Request(
                            Request::OnCameraSWState(_muted))),
                        _,
                    ) = message
                    {
                        handler_received_event = true;
                    }
                }
                complete => break,
            }
        }

        assert!(agent_received_sw_mute);
        assert!(handler_received_event);
    }

    // Tests that events are not sent to unavailable settings.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_sends_no_events_if_no_settings_available() {
        let service_message_hub = service::MessageHub::create_hub();
        let (messenger, publisher) =
            create_messenger_and_publisher_from_hub(&service_message_hub).await;
        let handler_address = service::Address::Handler(SettingType::Unknown);
        let verification_request = Request::Get;

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let mut handler_receptor: Receptor<Payload, Address, Role> = service_message_hub
            .create(MessengerType::Addressable(handler_address))
            .await
            .expect("Unable to create handler receptor")
            .1;

        // Declare all settings as unavailable so that no events are sent.
        let mut event_handler = EventHandler {
            publisher,
            messenger,
            recipient_settings: HashSet::new(),
            sw_muted: false,
        };

        // Send the events
        event_handler.handle_event(true);

        // Send an arbitrary request that should be the next payload received.
        service_message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create messenger")
            .0
            .message(
                HandlerPayload::Request(verification_request.clone()).into(),
                Audience::Address(handler_address),
            )
            .send()
            .ack();

        // Delete the messengers for the receptors we're selecting below. This will allow the while
        // loop below to eventually finish.
        service_message_hub.delete(handler_receptor.get_signature());

        assert_matches!(
            handler_receptor.next_of::<HandlerPayload>().await,
            Ok((HandlerPayload::Request(request), _))
                if request == verification_request
        )
    }
}
