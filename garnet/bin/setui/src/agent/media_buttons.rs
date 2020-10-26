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
use fuchsia_syslog::fx_log_err;
use futures::StreamExt;

blueprint_definition!(Descriptor::Component("buttons_agent"), MediaButtonsAgent::create);

pub struct MediaButtonsAgent {
    publisher: Publisher,
    switchboard_messenger: switchboard::message::Messenger,
    audio_available: bool,
    input_available: bool,
}

impl MediaButtonsAgent {
    pub async fn create(mut context: AgentContext) {
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
            audio_available: context.available_components.contains(&SettingType::Audio),
            input_available: context.available_components.contains(&SettingType::Input),
        };

        fasync::Task::spawn(async move {
            while let Ok((payload, client)) = context.agent_receptor.next_payload().await {
                if let Payload::Invocation(invocation) = payload {
                    client.reply(Payload::Complete(agent.handle(invocation).await)).send().ack();
                }
            }
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
            audio_available: self.audio_available,
            input_available: self.input_available,
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
    audio_available: bool,
    input_available: bool,
}

impl EventHandler {
    fn handle_event(&self, event: MediaButtonsEvent) {
        if let Some(volume_gain) = event.volume {
            self.handle_volume(volume_gain);
        }

        if let Some(mic_mute) = event.mic_mute {
            self.send_event(ButtonType::MicrophoneMute(mic_mute));
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

        if self.audio_available {
            self.switchboard_messenger
                .message(
                    switchboard::Payload::Action(switchboard::Action::Request(
                        SettingType::Audio,
                        setting_request.clone(),
                    )),
                    Audience::Address(switchboard::Address::Switchboard),
                )
                .send();
        }

        if self.input_available {
            self.switchboard_messenger
                .message(
                    switchboard::Payload::Action(switchboard::Action::Request(
                        SettingType::Input,
                        setting_request,
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
    use crate::internal::event;
    use crate::internal::switchboard;
    use crate::message::base::{MessageEvent, MessengerType};
    use crate::service_context::ServiceContext;
    use crate::tests::fakes::service_registry::ServiceRegistry;

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
            audio_available: false,
            input_available: false,
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
            audio_available: false,
            input_available: false,
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

        let event_handler = EventHandler {
            publisher,
            switchboard_messenger,
            audio_available: true,
            input_available: true,
        };

        // Send the events
        event_handler.handle_event(MediaButtonsEvent {
            volume: Some(1),
            mic_mute: Some(true),
            pause: None,
            camera_disable: None,
        });

        // Delete the messengers for the receptors we're selecting below. This
        // will allow the `select!` to eventually hit the `complete` case.
        switchboard_message_hub.delete(switchboard_signature);
        event_message_hub.delete(event_signature);

        let (
            mut agent_received_volume,
            mut agent_received_mic_mute,
            mut audio_received,
            mut input_received,
        ) = (false, false, false, false);

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
                                ButtonType::MicrophoneMute(_)
                            ) => {
                                agent_received_mic_mute = true;
                            }
                            event::media_buttons::Event::OnVolume(_) => {
                                agent_received_volume = true
                            }
                            _ => {}
                        }
                    }
                },
                message = fused_switchboard.select_next_some() => {
                    if let MessageEvent::Message(
                        switchboard::Payload::Action(switchboard::Action::Request(
                            setting_type,
                            SettingRequest::OnButton(ButtonType::MicrophoneMute(true)),
                        )),
                        _,
                    ) = message
                    {
                        match setting_type {
                            SettingType::Audio => audio_received = true,
                            SettingType::Input => input_received = true,
                            _ => {}
                        }
                    }
                }
                complete => break,
            }
        }

        assert!(agent_received_volume);
        assert!(agent_received_mic_mute);
        assert!(audio_received);
        assert!(input_received);
    }

    // Tests that messages are not sent to the audio setting when the audio
    // controller is not registered.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_does_not_send_event_to_audio_when_audio_disabled() {
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

        let event_handler = EventHandler {
            publisher,
            switchboard_messenger,
            audio_available: false,
            input_available: true,
        };

        // Send the events
        event_handler.handle_event(MediaButtonsEvent {
            volume: None,
            mic_mute: Some(true),
            pause: None,
            camera_disable: None,
        });

        // Delete the messenger for the receptor we're iterating below. This
        // will allow the receptor to close.
        switchboard_message_hub.delete(switchboard_signature);

        let (mut audio_received, mut input_received) = (false, false);
        while let Some(message) = switchboard_receptor.next().await {
            if let MessageEvent::Message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    setting_type,
                    SettingRequest::OnButton(ButtonType::MicrophoneMute(true)),
                )),
                _,
            ) = message
            {
                match setting_type {
                    SettingType::Audio => audio_received = true,
                    SettingType::Input => input_received = true,
                    _ => {}
                }
            }
        }

        assert!(!audio_received);
        assert!(input_received);
    }

    // Tests that messages are not sent to the input setting when the input
    // controller is not registered.
    #[fuchsia_async::run_until_stalled(test)]
    async fn event_handler_does_not_send_event_to_input_when_input_disabled() {
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

        let event_handler = EventHandler {
            publisher,
            switchboard_messenger,
            audio_available: true,
            input_available: false,
        };

        // Send the events
        event_handler.handle_event(MediaButtonsEvent {
            volume: None,
            mic_mute: Some(true),
            pause: None,
            camera_disable: None,
        });

        // Delete the messenger for the receptor we're iterating below. This
        // will allow the receptor to close.
        switchboard_message_hub.delete(switchboard_signature);

        let (mut audio_received, mut input_received) = (false, false);
        while let Some(message) = switchboard_receptor.next().await {
            if let MessageEvent::Message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    setting_type,
                    SettingRequest::OnButton(ButtonType::MicrophoneMute(true)),
                )),
                _,
            ) = message
            {
                match setting_type {
                    SettingType::Audio => audio_received = true,
                    SettingType::Input => input_received = true,
                    _ => {}
                }
            }
        }

        assert!(audio_received);
        assert!(!input_received);
    }
}
