// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{
    AgentError, Context as AgentContext, Descriptor, Invocation, InvocationResult, Lifespan,
};
use crate::blueprint_definition;
use crate::input::common::ButtonType;
use crate::input::{monitor_media_buttons, VolumeGain};
use crate::internal::agent::Payload;
use crate::internal::event::{media_buttons, Event, Publisher};
use crate::internal::switchboard;
use crate::message::base::Audience;
use crate::service_context::ServiceContextHandle;
use crate::switchboard::base::{SettingRequest, SettingType};
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::StreamExt;
use std::collections::HashSet;

blueprint_definition!(Descriptor::Component("buttons_agent"), MediaButtonsAgent::create);

/// Setting types that the media buttons agent will send media button events to, if they're
/// available on the device.
fn get_event_setting_types() -> HashSet<SettingType> {
    vec![SettingType::Audio, SettingType::Input].into_iter().collect()
}

pub struct MediaButtonsAgent {
    publisher: Publisher,
    switchboard_messenger: switchboard::message::Messenger,

    /// Settings to send media buttons events to.
    recipient_settings: HashSet<SettingType>,
}

impl MediaButtonsAgent {
    pub async fn create(context: AgentContext) {
        let switchboard_messenger =
            if let Ok(messenger) = context.create_switchboard_messenger().await {
                messenger
            } else {
                context.get_publisher().send_event(Event::Custom(
                    "Could not acquire switchboard messenger in MediaButtonsAgent",
                ));
                return;
            };

        let mut agent = MediaButtonsAgent {
            publisher: context.get_publisher(),
            switchboard_messenger,
            recipient_settings: context
                .available_components
                .intersection(&get_event_setting_types())
                .cloned()
                .collect::<HashSet<SettingType>>(),
        };

        let mut receptor = context.receptor;
        fasync::Task::spawn(async move {
            while let Ok((payload, client)) = receptor.next_payload().await {
                if let Payload::Invocation(invocation) = payload {
                    client.reply(Payload::Complete(agent.handle(invocation).await)).send().ack();
                }
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
        service_context: ServiceContextHandle,
    ) -> InvocationResult {
        let (input_tx, mut input_rx) = futures::channel::mpsc::unbounded::<MediaButtonsEvent>();
        if let Err(e) = monitor_media_buttons(service_context, input_tx).await {
            fx_log_err!("Unable to monitor media buttons: {:?}", e);
            return Err(AgentError::UnexpectedError);
        }

        let event_handler = EventHandler {
            publisher: self.publisher.clone(),
            switchboard_messenger: self.switchboard_messenger.clone(),
            recipient_settings: self.recipient_settings.clone(),
        };
        fasync::Task::spawn(async move {
            while let Some(event) = input_rx.next().await {
                event_handler.handle_event(event);
            }
        })
        .detach();

        Ok(())
    }
}

struct EventHandler {
    publisher: Publisher,
    switchboard_messenger: switchboard::message::Messenger,
    recipient_settings: HashSet<SettingType>,
}

impl EventHandler {
    fn handle_event(&self, event: MediaButtonsEvent) {
        if let Some(volume_gain) = event.volume {
            self.handle_volume(volume_gain);
        }

        if let Some(mic_mute) = event.mic_mute {
            self.send_event(ButtonType::MicrophoneMute(mic_mute));
        }

        if let Some(camera_disable) = event.camera_disable {
            self.send_event(ButtonType::CameraDisable(camera_disable));
        }
    }

    fn handle_volume(&self, volume_gain: i8) {
        let volume_gain = match volume_gain {
            -1 => VolumeGain::Down,
            0 => VolumeGain::Neutral,
            1 => VolumeGain::Up,
            _ => {
                fx_log_err!("Invalid volume gain value: {}", volume_gain);
                return;
            }
        };
        self.send_event(volume_gain);
    }

    fn send_event<E>(&self, event: E)
    where
        E: Copy + Into<media_buttons::Event> + Into<SettingRequest> + std::fmt::Debug,
    {
        self.publisher.send_event(Event::MediaButtons(event.into()));
        let setting_request: SettingRequest = event.into();

        // Send the event to all the interested setting types that are also available.
        for setting_type in self.recipient_settings.iter() {
            self.switchboard_messenger
                .message(
                    switchboard::Payload::Action(switchboard::Action::Request(
                        *setting_type,
                        setting_request.clone(),
                    )),
                    Audience::Address(switchboard::Address::Switchboard),
                )
                .send();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::input::common::MediaButtonsEventBuilder;
    use crate::internal::event;
    use crate::internal::switchboard;
    use crate::message::base::{MessageEvent, MessengerType};
    use crate::service_context::ServiceContext;
    use crate::tests::fakes::service_registry::ServiceRegistry;
    use std::collections::HashMap;
    use std::iter::FromIterator;

    // TODO(fxbug.dev/62860): Refactor tests, could use a common setup helper.

    // Tests that the initialization lifespan is not handled.
    #[fuchsia_async::run_until_stalled(test)]
    async fn initialization_lifespan_is_unhandled() {
        // Setup messengers needed to construct the agent.
        let event_message_hub = event::message::create_hub();
        let switchboard_message_hub = switchboard::message::create_hub();
        let publisher = Publisher::create(&event_message_hub, MessengerType::Unbound).await;

        let (switchboard_messenger, _) = switchboard_message_hub
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("Unable to create switchboard messenger");

        // Construct the agent.
        let mut agent = MediaButtonsAgent {
            publisher,
            switchboard_messenger,
            recipient_settings: HashSet::new(),
        };

        // Try to initiatate the initialization lifespan.
        let result = agent
            .handle(Invocation {
                lifespan: Lifespan::Initialization,
                service_context: ServiceContext::create(None, None),
            })
            .await;

        assert!(matches!(result, Err(AgentError::UnhandledLifespan)));
    }

    // Tests that the agent cannot start without a media buttons service.
    #[fuchsia_async::run_until_stalled(test)]
    async fn when_media_buttons_inaccessible_returns_err() {
        // Setup messengers needed to construct the agent.
        let event_message_hub = event::message::create_hub();
        let switchboard_message_hub = switchboard::message::create_hub();
        let publisher = Publisher::create(&event_message_hub, MessengerType::Unbound).await;

        let (switchboard_messenger, _) = switchboard_message_hub
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("Unable to create switchboard messenger");

        // Construct the agent.
        let mut agent = MediaButtonsAgent {
            publisher,
            switchboard_messenger,
            recipient_settings: HashSet::new(),
        };

        let service_context = ServiceContext::create(
            // Create a service registry without a media buttons interface.
            Some(ServiceRegistry::serve(ServiceRegistry::create())),
            None,
        );

        // Try to initiate the Service lifespan without providing the MediaButtons fidl interface.
        let result =
            agent.handle(Invocation { lifespan: Lifespan::Service, service_context }).await;
        assert!(matches!(result, Err(AgentError::UnexpectedError)));
    }

    // Tests that events can be sent to the intended recipients.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_proxies_event() {
        let event_message_hub = event::message::create_hub();
        let switchboard_message_hub = switchboard::message::create_hub();

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let (event_signature, event_receptor) = {
            let (messenger, receptor) = event_message_hub
                .create(MessengerType::Unbound)
                .await
                .expect("Unable to create agent receptor");
            (messenger.get_signature(), receptor)
        };
        let publisher = Publisher::create(&event_message_hub, MessengerType::Unbound).await;

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let (switchboard_signature, switchboard_receptor) = {
            let (messenger, receptor) = switchboard_message_hub
                .create(MessengerType::Addressable(switchboard::Address::Switchboard))
                .await
                .expect("Unable to create switchboard receptor");
            (messenger.get_signature(), receptor)
        };
        let (switchboard_messenger, _) = switchboard_message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create switchboard messenger");

        // Make all setting types available.
        let available_components = HashSet::from_iter(get_event_setting_types());
        let event_handler = EventHandler {
            publisher,
            switchboard_messenger,
            recipient_settings: available_components,
        };

        // Send the events.
        event_handler.handle_event(
            MediaButtonsEventBuilder::new()
                .set_volume(1)
                .set_mic_mute(true)
                .set_camera_disable(true)
                .build(),
        );

        // Delete the messengers for the receptors we're selecting below. This
        // will allow the `select!` to eventually hit the `complete` case.
        switchboard_message_hub.delete(switchboard_signature);
        event_message_hub.delete(event_signature);

        let (
            mut agent_received_volume,
            mut agent_received_mic_mute,
            mut agent_received_camera_disable,
        ) = (false, false, false);

        let mut received_events: HashMap<SettingType, u32> = HashMap::new();

        let fused_event = event_receptor.fuse();
        let fused_switchboard = switchboard_receptor.fuse();
        futures::pin_mut!(fused_event, fused_switchboard);

        // Loop over the select so we can handle the messages as they come in. When all messages
        // have been handled, due to the messengers being deleted above, the complete branch should
        // be hit to break out of the loop.
        loop {
            futures::select! {
                message = fused_event.select_next_some() => {
                    if let MessageEvent::Message(event::Payload::Event(
                        event::Event::MediaButtons(event)
                    ), _) = message
                    {
                        match event {
                            event::media_buttons::Event::OnButton(
                                ButtonType::MicrophoneMute(muted)
                            ) => {
                                assert!(muted);
                                agent_received_mic_mute = true;
                            }
                            event::media_buttons::Event::OnVolume(state) => {
                                assert_eq!(state, VolumeGain::Up);
                                agent_received_volume = true;
                            }
                            event::media_buttons::Event::OnButton(
                                ButtonType::CameraDisable(disabled)
                            ) => {
                                assert!(disabled);
                                agent_received_camera_disable = true;
                            }
                            _ => {}
                        }
                    }
                },
                message = fused_switchboard.select_next_some() => {
                    if let MessageEvent::Message(
                        switchboard::Payload::Action(switchboard::Action::Request(
                            setting_type,
                            SettingRequest::OnButton(button),
                        )),
                        _,
                    ) = message
                    {
                        *received_events.entry(setting_type).or_default() += 1;
                    }
                }
                complete => break,
            }
        }

        assert!(agent_received_volume);
        assert!(agent_received_mic_mute);
        assert!(agent_received_camera_disable);

        // Verify that we received events for eacn of the expected settings.
        for setting_type in get_event_setting_types() {
            // Each setting should have received two events, one for mic and one for camera.
            assert_eq!(*received_events.entry(setting_type).or_default(), 2);
        }
    }

    // Tests that events are not sent to unavailable settings.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_sends_no_events_if_no_settings_available() {
        let event_message_hub = event::message::create_hub();
        let switchboard_message_hub = switchboard::message::create_hub();
        let publisher = Publisher::create(&event_message_hub, MessengerType::Unbound).await;

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let (switchboard_signature, mut switchboard_receptor) = {
            let (messenger, receptor) = switchboard_message_hub
                .create(MessengerType::Addressable(switchboard::Address::Switchboard))
                .await
                .expect("Unable to create switchboard receptor");
            (messenger.get_signature(), receptor)
        };
        let (switchboard_messenger, _) = switchboard_message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create switchboard messenger");

        // Declare all settings as unavailable so that no events are sent.
        let event_handler =
            EventHandler { publisher, switchboard_messenger, recipient_settings: HashSet::new() };

        // Send the events
        event_handler.handle_event(
            MediaButtonsEventBuilder::new()
                .set_volume(1)
                .set_mic_mute(true)
                .set_camera_disable(true)
                .build(),
        );

        let mut received_events: HashMap<SettingType, u32> = HashMap::new();

        // Delete the messengers for the receptors we're selecting below. This will allow the while
        // loop below to eventually finish.
        switchboard_message_hub.delete(switchboard_signature);

        while let Some(message) = switchboard_receptor.next().await {
            if let MessageEvent::Message(
                switchboard::Payload::Action(switchboard::Action::Request(setting_type, _)),
                _,
            ) = message
            {
                *received_events.entry(setting_type).or_default() += 1;
            }
        }

        // No events were received via the switchboard.
        assert!(received_events.is_empty());
    }

    // Tests that events are only sent to settings that are available and none others.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_sends_events_to_available_settings() {
        let event_message_hub = event::message::create_hub();
        let switchboard_message_hub = switchboard::message::create_hub();
        let publisher = Publisher::create(&event_message_hub, MessengerType::Unbound).await;

        let (_, mut switchboard_receptor) = switchboard_message_hub
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("Unable to create switchboard receptor");

        // Run through the test once for each individual setting type among the ones that media
        // buttons sends events to.
        for setting_type in get_event_setting_types() {
            // Get the messenger's signature and the receptor for agents. We need
            // a different messenger below because a broadcast would not send a message
            // to itself. The signature is used to delete the original messenger for this
            // receptor.
            let (event_messenger, _) = switchboard_message_hub
                .create(MessengerType::Unbound)
                .await
                .expect("Unable to create switchboard messenger");

            // Declare only a single setting as available so that only it receives events.
            let mut available_components = HashSet::new();
            available_components.insert(setting_type);
            let event_handler = EventHandler {
                publisher: publisher.clone(),
                switchboard_messenger: event_messenger,
                recipient_settings: available_components,
            };

            // Send the events
            event_handler.handle_event(
                MediaButtonsEventBuilder::new()
                    .set_volume(1)
                    .set_mic_mute(true)
                    .set_camera_disable(true)
                    .build(),
            );

            let mut received_events: HashMap<SettingType, u32> = HashMap::new();

            while let Some(message) = switchboard_receptor.next().await {
                if let MessageEvent::Message(
                    switchboard::Payload::Action(switchboard::Action::Request(setting_type, _)),
                    _,
                ) = message
                {
                    *received_events.entry(setting_type).or_default() += 1;
                }
                // Only check the first event. If unexpected events were queued up, they'll be
                // detected in additional iterations.
                return;
            }

            // Only one setting type received events.
            assert_eq!(received_events.len(), 1);
            // All events were received by our specified setting type: for mic, volume, and camera.
            assert_eq!(*received_events.entry(setting_type).or_default(), 3);
        }
    }
}
