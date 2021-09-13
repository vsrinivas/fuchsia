// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::Payload;
use crate::agent::{AgentError, Context as AgentContext, Invocation, InvocationResult, Lifespan};
use crate::base::SettingType;
use crate::blueprint_definition;
use crate::event::{media_buttons, Event, Publisher};
use crate::handler::base::{Payload as HandlerPayload, Request};
use crate::handler::device_storage::DeviceStorageAccess;
use crate::input::{monitor_media_buttons, MediaButtons, VolumeGain};
use crate::message::base::Audience;
use crate::service;
use crate::service_context::ServiceContext;
use crate::trace::TracingNonce;
use crate::trace_guard;
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::StreamExt;
use std::collections::HashSet;
use std::sync::Arc;

blueprint_definition!("buttons_agent", MediaButtonsAgent::create);

/// Setting types that the media buttons agent will send media button events to, if they're
/// available on the device.
fn get_event_setting_types() -> HashSet<SettingType> {
    vec![SettingType::Audio, SettingType::Light, SettingType::Input].into_iter().collect()
}

pub(crate) struct MediaButtonsAgent {
    publisher: Publisher,
    messenger: service::message::Messenger,

    /// Settings to send media buttons events to.
    recipient_settings: HashSet<SettingType>,
}

impl DeviceStorageAccess for MediaButtonsAgent {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

impl MediaButtonsAgent {
    pub(crate) async fn create(context: AgentContext) {
        let mut agent = MediaButtonsAgent {
            publisher: context.get_publisher(),
            messenger: context.create_messenger().await.expect("media button messenger created"),
            recipient_settings: context
                .available_components
                .intersection(&get_event_setting_types())
                .cloned()
                .collect::<HashSet<SettingType>>(),
        };

        let mut receptor = context.receptor;
        fasync::Task::spawn(async move {
            while let Ok((Payload::Invocation(invocation), client)) =
                receptor.next_of::<Payload>().await
            {
                client.reply(Payload::Complete(agent.handle(invocation).await).into()).send().ack();
            }

            fx_log_info!("Media buttons agent done processing requests");
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
        let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
        if let Err(e) = monitor_media_buttons(service_context, input_tx).await {
            fx_log_err!("Unable to monitor media buttons: {:?}", e);
            return Err(AgentError::UnexpectedError);
        }

        let event_handler = EventHandler {
            publisher: self.publisher.clone(),
            messenger: self.messenger.clone(),
            recipient_settings: self.recipient_settings.clone(),
        };
        fasync::Task::spawn(async move {
            while let Some(event) = input_rx.next().await {
                let nonce = fuchsia_trace::generate_nonce();
                event_handler.handle_event(event, nonce);
            }
        })
        .detach();

        Ok(())
    }
}

struct EventHandler {
    publisher: Publisher,
    messenger: service::message::Messenger,
    recipient_settings: HashSet<SettingType>,
}

impl EventHandler {
    fn handle_event(&self, event: MediaButtonsEvent, nonce: TracingNonce) {
        if let Some(volume_gain) = event.volume {
            self.handle_volume(volume_gain, nonce);
        }

        if event.mic_mute.is_some() || event.camera_disable.is_some() {
            let media_buttons: MediaButtons = event.into();
            self.send_event(media_buttons, nonce);
        }
    }

    fn handle_volume(&self, volume_gain: i8, nonce: TracingNonce) {
        let volume_gain = match volume_gain {
            -1 => VolumeGain::Down,
            0 => VolumeGain::Neutral,
            1 => VolumeGain::Up,
            _ => {
                fx_log_err!("Invalid volume gain value: {}", volume_gain);
                return;
            }
        };
        self.send_event(volume_gain, nonce);
    }

    fn send_event<E>(&self, event: E, nonce: TracingNonce)
    where
        E: Copy + Into<media_buttons::Event> + Into<Request> + std::fmt::Debug,
    {
        self.publisher.send_event(Event::MediaButtons(event.into()));
        let setting_request: Request = event.into();

        // Send the event to all the interested setting types that are also available.
        for setting_type in self.recipient_settings.iter() {
            let guard = trace_guard!(
                nonce,

                "media buttons send event",
                "setting_type" => format!("{:?}", setting_type).as_str()
            );
            let mut receptor = self
                .messenger
                .message(
                    HandlerPayload::Request(setting_request.clone()).into(),
                    Audience::Address(service::Address::Handler(*setting_type)),
                )
                .send();
            fasync::Task::spawn(async move {
                while let Ok((_response, _)) = receptor.next_payload().await {
                    drop(guard);
                    break;
                }
            })
            .detach();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::event;
    use crate::input::common::MediaButtonsEventBuilder;
    use crate::input::MediaButtons;
    use crate::message::base::MessageEvent;
    use crate::message::receptor::Receptor;
    use crate::message::MessageHubUtil;
    use crate::service;
    use crate::service::{Address, Payload, Role};
    use crate::service_context::ServiceContext;
    use crate::tests::fakes::service_registry::ServiceRegistry;
    use crate::tests::helpers::{
        create_messenger_and_publisher, create_messenger_and_publisher_from_hub,
        create_receptor_for_setting_type,
    };

    // Tests that the initialization lifespan is not handled.
    #[fuchsia_async::run_until_stalled(test)]
    async fn initialization_lifespan_is_unhandled() {
        // Setup messengers needed to construct the agent.
        let (messenger, publisher) = create_messenger_and_publisher().await;

        // Construct the agent.
        let mut agent =
            MediaButtonsAgent { publisher, messenger, recipient_settings: HashSet::new() };

        // Try to initiatate the initialization lifespan.
        let result = agent
            .handle(Invocation {
                lifespan: Lifespan::Initialization,
                service_context: Arc::new(ServiceContext::new(None, None)),
            })
            .await;

        assert!(matches!(result, Err(AgentError::UnhandledLifespan)));
    }

    // Tests that the agent cannot start without a media buttons service.
    #[fuchsia_async::run_until_stalled(test)]
    async fn when_media_buttons_inaccessible_returns_err() {
        // Setup messengers needed to construct the agent.
        let (messenger, publisher) = create_messenger_and_publisher().await;

        // Construct the agent.
        let mut agent =
            MediaButtonsAgent { publisher, messenger, recipient_settings: HashSet::new() };

        let service_context = Arc::new(ServiceContext::new(
            // Create a service registry without a media buttons interface.
            Some(ServiceRegistry::serve(ServiceRegistry::create())),
            None,
        ));

        // Try to initiate the Service lifespan without providing the MediaButtons fidl interface.
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
        let target_setting_type = SettingType::Unknown;

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let event_receptor = service::build_event_listener(&service_message_hub).await;

        // Create receptor representing handler endpoint.
        let handler_receptor: Receptor<Payload, Address, Role> =
            create_receptor_for_setting_type(&service_message_hub, target_setting_type).await;

        // Make all setting types available.
        let event_handler = EventHandler {
            publisher,
            messenger,
            recipient_settings: vec![target_setting_type].into_iter().collect(),
        };

        // Send the events.
        event_handler.handle_event(
            MediaButtonsEventBuilder::new()
                .set_volume(1)
                .set_mic_mute(true)
                .set_camera_disable(true)
                .build(),
            0,
        );

        // Delete the messengers for the receptors we're selecting below. This
        // will allow the `select!` to eventually hit the `complete` case.
        service_message_hub.delete(handler_receptor.get_signature());
        service_message_hub.delete(event_receptor.get_signature());

        let (mut agent_received_volume, mut agent_received_media_buttons) = (false, false);

        let mut received_events: usize = 0;

        let fused_event = event_receptor.fuse();
        let fused_handler = handler_receptor.fuse();
        futures::pin_mut!(fused_event, fused_handler);

        // Loop over the select so we can handle the messages as they come in. When all messages
        // have been handled, due to the messengers being deleted above, the complete branch should
        // be hit to break out of the loop.
        loop {
            futures::select! {
                message = fused_event.select_next_some() => {
                    if let MessageEvent::Message(
                        service::Payload::Event(event::Payload::Event(
                            event::Event::MediaButtons(event))), _) = message
                    {
                        match event {
                            event::media_buttons::Event::OnButton(
                                MediaButtons{..}
                            ) => {
                                agent_received_media_buttons = true;
                            }
                            event::media_buttons::Event::OnVolume(state) => {
                                assert_eq!(state, VolumeGain::Up);
                                agent_received_volume = true;
                            }
                        }
                    }
                },
                message = fused_handler.select_next_some() => {
                    if let MessageEvent::Message(
                        service::Payload::Setting(HandlerPayload::Request(
                            Request::OnButton(_button),
                        )),
                        _,
                    ) = message
                    {
                        received_events += 1;
                    }
                }
                complete => break,
            }
        }

        assert!(agent_received_volume);
        assert!(agent_received_media_buttons);

        // setting should have received one event for both mic and camera.
        assert_eq!(received_events, 1);
    }

    // Tests that events are not sent to unavailable settings.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_sends_no_events_if_no_settings_available() {
        let service_message_hub = service::MessageHub::create_hub();
        let (messenger, publisher) =
            create_messenger_and_publisher_from_hub(&service_message_hub).await;

        // Create messenger to represent unavailable setting handler.
        let mut handler_receptor: Receptor<Payload, Address, Role> =
            create_receptor_for_setting_type(&service_message_hub, SettingType::Unknown).await;

        // Declare all settings as unavailable so that no events are sent.
        let event_handler =
            EventHandler { publisher, messenger, recipient_settings: HashSet::new() };

        // Send the events
        event_handler.handle_event(
            MediaButtonsEventBuilder::new()
                .set_volume(1)
                .set_mic_mute(true)
                .set_camera_disable(true)
                .build(),
            0,
        );

        let mut received_events: usize = 0;

        // Delete the messengers for the receptors we're selecting below. This will allow the while
        // loop below to eventually finish.
        service_message_hub.delete(handler_receptor.get_signature());

        while let Ok((HandlerPayload::Request(_), _)) =
            handler_receptor.next_of::<HandlerPayload>().await
        {
            received_events += 1;
        }

        // No events were received via the setting handler.
        assert_eq!(received_events, 0);
    }
}
