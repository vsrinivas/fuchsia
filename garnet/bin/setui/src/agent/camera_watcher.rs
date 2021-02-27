// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::base::{
    AgentError, Context as AgentContext, Invocation, InvocationResult, Lifespan,
};
use crate::base::SettingType;
use crate::blueprint_definition;
use crate::handler::base::Request;
use crate::handler::device_storage::DeviceStorageAccess;
use crate::input::common::connect_to_camera;
use crate::internal::agent::Payload;
use crate::internal::event::{camera_watcher, Event, Publisher};
use crate::internal::switchboard;
use crate::message::base::Audience;
use crate::service_context::ServiceContextHandle;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use std::collections::HashSet;

blueprint_definition!("camera_watcher_agent", CameraWatcherAgent::create);

/// Setting types that the camera watcher agent will send updates to, if they're
/// available on the device.
fn get_event_setting_types() -> HashSet<SettingType> {
    vec![SettingType::Input].into_iter().collect()
}

// TODO(fxbug.dev/70195): Extract common template from agents.
pub struct CameraWatcherAgent {
    publisher: Publisher,
    switchboard_messenger: switchboard::message::Messenger,

    /// Settings to send camera watcher events to.
    recipient_settings: HashSet<SettingType>,
}

impl DeviceStorageAccess for CameraWatcherAgent {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

impl CameraWatcherAgent {
    pub async fn create(context: AgentContext) {
        let switchboard_messenger =
            if let Ok(messenger) = context.create_switchboard_messenger().await {
                messenger
            } else {
                context.get_publisher().send_event(Event::Custom(
                    "Could not acquire switchboard messenger in CameraWatcherAgent",
                ));
                return;
            };

        let mut agent = CameraWatcherAgent {
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
        service_context: ServiceContextHandle,
    ) -> InvocationResult {
        match connect_to_camera(service_context).await {
            Ok(camera_device_client) => {
                let mut event_handler = EventHandler {
                    publisher: self.publisher.clone(),
                    switchboard_messenger: self.switchboard_messenger.clone(),
                    recipient_settings: self.recipient_settings.clone(),
                    sw_muted: false,
                };
                fasync::Task::spawn(async move {
                    // Here we don't care about hw_muted state because the input service would pick up
                    // mute changes directly from the switch. We care about sw changes because other
                    // clients of the camera3 service could change the sw mute state but not notify
                    // the settings service.
                    while let Ok((sw_muted, _hw_muted)) =
                        camera_device_client.watch_mute_state().await
                    {
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
    switchboard_messenger: switchboard::message::Messenger,
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
    use crate::internal::event;
    use crate::internal::switchboard;
    use crate::message::base::{MessageEvent, MessengerType};
    use crate::service_context::ServiceContext;
    use crate::tests::fakes::service_registry::ServiceRegistry;
    use futures::StreamExt;
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
        let mut agent = CameraWatcherAgent {
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

    // Tests that the agent cannot start without a camera3 service.
    #[fuchsia_async::run_until_stalled(test)]
    async fn when_camera3_inaccessible_returns_err() {
        // Setup messengers needed to construct the agent.
        let event_message_hub = event::message::create_hub();
        let switchboard_message_hub = switchboard::message::create_hub();
        let publisher = Publisher::create(&event_message_hub, MessengerType::Unbound).await;

        let (switchboard_messenger, _) = switchboard_message_hub
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("Unable to create switchboard messenger");

        // Construct the agent.
        let mut agent = CameraWatcherAgent {
            publisher,
            switchboard_messenger,
            recipient_settings: HashSet::new(),
        };

        let service_context = ServiceContext::create(
            // Create a service registry without a camera3 service interface.
            Some(ServiceRegistry::serve(ServiceRegistry::create())),
            None,
        );

        // Try to initiate the Service lifespan without providing the camera3 fidl interface.
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
        let event_receptor = event_message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create agent receptor")
            .1;

        let publisher = Publisher::create(&event_message_hub, MessengerType::Unbound).await;

        // Get the messenger's signature and the receptor for agents. We need
        // a different messenger below because a broadcast would not send a message
        // to itself. The signature is used to delete the original messenger for this
        // receptor.
        let switchboard_receptor = switchboard_message_hub
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("Unable to create switchboard receptor")
            .1;
        let (switchboard_messenger, _) = switchboard_message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create switchboard messenger");

        // Make all setting types available.
        let available_components = HashSet::from_iter(get_event_setting_types());
        let mut event_handler = EventHandler {
            publisher,
            switchboard_messenger,
            recipient_settings: available_components,
            sw_muted: false,
        };

        // Send the events.
        event_handler.handle_event(true);

        // Delete the messengers for the receptors we're selecting below. This
        // will allow the `select!` to eventually hit the `complete` case.
        switchboard_message_hub.delete(switchboard_receptor.get_signature());
        event_message_hub.delete(event_receptor.get_signature());

        let mut agent_received_sw_mute = false;
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
                        event::Event::CameraUpdate(event)
                    ), _) = message
                    {
                        match event {
                            event::camera_watcher::Event::OnSWMuteState(muted) => {
                                assert!(muted);
                                agent_received_sw_mute = true;
                            }
                        }
                    }
                },
                message = fused_switchboard.select_next_some() => {
                    if let MessageEvent::Message(
                        switchboard::Payload::Action(switchboard::Action::Request(
                            setting_type,
                            Request::OnCameraSWState(_muted),
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

        assert!(agent_received_sw_mute);

        // Verify that we received events for eacn of the expected settings.
        for setting_type in get_event_setting_types() {
            assert_eq!(*received_events.entry(setting_type).or_default(), 1);
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
        let mut switchboard_receptor = switchboard_message_hub
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .expect("Unable to create switchboard receptor")
            .1;
        let (switchboard_messenger, _) = switchboard_message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create switchboard messenger");

        // Declare all settings as unavailable so that no events are sent.
        let mut event_handler = EventHandler {
            publisher,
            switchboard_messenger,
            recipient_settings: HashSet::new(),
            sw_muted: false,
        };

        // Send the events
        event_handler.handle_event(true);

        let mut received_events: HashMap<SettingType, u32> = HashMap::new();

        // Delete the messengers for the receptors we're selecting below. This will allow the while
        // loop below to eventually finish.
        switchboard_message_hub.delete(switchboard_receptor.get_signature());

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

    // Tests that events are only sent to settings that are available and no others.
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
            let mut event_handler = EventHandler {
                publisher: publisher.clone(),
                switchboard_messenger: event_messenger,
                recipient_settings: available_components,
                sw_muted: false,
            };

            // Send the events
            event_handler.handle_event(true);

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
            assert_eq!(*received_events.entry(setting_type).or_default(), 1);
        }
    }
}
