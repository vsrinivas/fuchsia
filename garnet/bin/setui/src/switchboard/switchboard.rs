// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::{Error, Request};
use crate::internal::core;
use crate::internal::switchboard;
use crate::message::action_fuse::ActionFuseBuilder;
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::switchboard::base::{SettingAction, SettingActionData, SettingEvent};

use fuchsia_async as fasync;
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::stream::StreamExt;
use futures::FutureExt;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::result::Result::Ok;
use std::sync::Arc;

type SwitchboardListenerMap = HashMap<SettingType, Vec<switchboard::message::MessageClient>>;

pub struct SwitchboardBuilder {
    core_messenger_factory: Option<core::message::Factory>,
    switchboard_messenger_factory: Option<switchboard::message::Factory>,
    setting_proxies: HashMap<SettingType, core::message::Signature>,
    policy_proxies: HashMap<core::message::Signature, SettingType>,
}

impl SwitchboardBuilder {
    pub fn create() -> Self {
        SwitchboardBuilder {
            core_messenger_factory: None,
            switchboard_messenger_factory: None,
            setting_proxies: HashMap::new(),
            policy_proxies: HashMap::new(),
        }
    }

    pub fn switchboard_messenger_factory(mut self, factory: switchboard::message::Factory) -> Self {
        self.switchboard_messenger_factory = Some(factory);
        self
    }

    pub fn core_messenger_factory(mut self, factory: core::message::Factory) -> Self {
        self.core_messenger_factory = Some(factory);
        self
    }

    pub fn add_setting_proxies(
        mut self,
        proxies: HashMap<SettingType, core::message::Signature>,
    ) -> Self {
        self.setting_proxies.extend(proxies);
        self
    }

    pub fn add_setting_proxy(
        mut self,
        setting_type: SettingType,
        signature: core::message::Signature,
    ) -> Self {
        self.setting_proxies.insert(setting_type, signature);
        self
    }

    pub fn add_policy_proxies(
        mut self,
        policy_proxies: HashMap<core::message::Signature, SettingType>,
    ) -> Self {
        self.policy_proxies.extend(policy_proxies);
        self
    }
    pub async fn build(self) -> Result<(), anyhow::Error> {
        Switchboard::create(
            self.core_messenger_factory.unwrap_or(core::message::create_hub()),
            self.switchboard_messenger_factory.unwrap_or(switchboard::message::create_hub()),
            self.setting_proxies,
            self.policy_proxies,
        )
        .await
    }
}

pub struct Switchboard {
    /// Next available action id.
    next_action_id: u64,
    /// Passed as with an `ActionFuse` to listen clients to capture when
    /// the listen session goes out of scope.
    listen_cancellation_sender: UnboundedSender<(SettingType, switchboard::message::MessageClient)>,
    /// mapping of listeners for changes
    listeners: SwitchboardListenerMap,
    /// core messenger
    core_messenger: core::message::Messenger,
    /// Active setting proxies
    setting_proxies: HashMap<SettingType, core::message::Signature>,
    /// Mapping from proxy to [`SettingType`].
    proxy_settings: HashMap<core::message::Signature, SettingType>,
}

impl Switchboard {
    /// Creates a new Switchboard, which will return the instance along with
    /// a sender to provide events in response to the actions sent.
    ///
    /// Requests will be recorded to the given inspect node.
    async fn create(
        core_messenger_factory: core::message::Factory,
        switchboard_messenger_factory: switchboard::message::Factory,
        setting_proxies: HashMap<SettingType, core::message::Signature>,
        policy_proxies: HashMap<core::message::Signature, SettingType>,
    ) -> Result<(), anyhow::Error> {
        let (cancel_listen_tx, mut cancel_listen_rx) = futures::channel::mpsc::unbounded::<(
            SettingType,
            switchboard::message::MessageClient,
        )>();

        let (core_messenger, mut core_receptor) = core_messenger_factory
            .create(MessengerType::Addressable(core::Address::Switchboard))
            .await
            .map_err(anyhow::Error::new)?;

        let (_, mut switchboard_receptor) = switchboard_messenger_factory
            .create(MessengerType::Addressable(switchboard::Address::Switchboard))
            .await
            .map_err(anyhow::Error::new)?;

        let mut proxy_settings = HashMap::new();

        for (key, value) in &setting_proxies {
            proxy_settings.insert(value.clone(), key.clone());
        }

        // Add policy proxies, since they can directly send SettingEvents to the switchboard.
        // TODO(fxbug.dev/67695): migrate to a refresh command instead of talking directly to the
        // switchboard.
        proxy_settings.extend(policy_proxies);

        let switchboard = Arc::new(Mutex::new(Self {
            next_action_id: 0,
            listen_cancellation_sender: cancel_listen_tx,
            listeners: HashMap::new(),
            core_messenger,
            setting_proxies,
            proxy_settings,
        }));

        let switchboard_clone = switchboard.clone();

        fasync::Task::spawn(async move {
            loop {
                let core_receptor = core_receptor.next().fuse();
                let switchboard_receptor = switchboard_receptor.next().fuse();
                let cancel_receptor = cancel_listen_rx.next().fuse();
                futures::pin_mut!(core_receptor, switchboard_receptor, cancel_receptor);

                futures::select! {
                    // Invoked when there is a new message from the proxies.
                    core_event = core_receptor => {
                        if let Some(MessageEvent::Message(core::Payload::Event(event), message_client)) = core_event {
                            switchboard_clone.lock().await.process_event(event, message_client.get_author());
                        }
                    }
                    // Invoked when there is a new message from the switchboard
                    // message interface.
                    switchboard_event = switchboard_receptor => {
                        if let Some(MessageEvent::Message(payload, message_client)) = switchboard_event {
                            match payload {
                                switchboard::Payload::Action(switchboard::Action::Request(setting_type, request)) => {
                                    switchboard_clone.lock().await.process_action_request(setting_type, request, message_client).ok();
                                }
                                switchboard::Payload::Listen(switchboard::Listen::Request(setting_type)) => {
                                    switchboard_clone.lock().await.process_listen_request(setting_type, message_client).await;
                                }
                                _ => {
                                }
                            }
                        }
                    }
                    // Invoked when listener drops `MessageClient` associated
                    // with a listen request.
                    cancel_event = cancel_receptor => {
                        if let Some((setting_type, client)) = cancel_event {
                            switchboard_clone.lock()
                            .await
                            .remove_setting_listener(setting_type, client)
                            .await;
                        }

                    }
                }
            }
        }).detach();

        return Ok(());
    }

    pub fn get_next_action_id(&mut self) -> u64 {
        let return_id = self.next_action_id;
        self.next_action_id += 1;
        return return_id;
    }

    fn process_event(&mut self, input: SettingEvent, author: core::message::Signature) {
        match input {
            // TODO(fxb/66295): notify listeners of the new value directly.
            SettingEvent::Changed(setting_info) => {
                let setting_type =
                    self.proxy_settings.get(&author).expect("should match setting type");
                self.notify_listeners(setting_type, setting_info);
            }
            _ => {}
        }
    }

    async fn process_listen_request(
        &mut self,
        setting_type: SettingType,
        mut reply_client: switchboard::message::MessageClient,
    ) {
        let cancellation_sender = self.listen_cancellation_sender.clone();
        let client = reply_client.clone();
        reply_client
            .bind_to_recipient(
                ActionFuseBuilder::new()
                    .add_action(Box::new(move || {
                        cancellation_sender.unbounded_send((setting_type, client.clone())).ok();
                    }))
                    .build(),
            )
            .await;

        if !self.listeners.contains_key(&setting_type) {
            self.listeners.insert(setting_type, vec![]);
        }

        self.listeners.entry(setting_type).or_insert(vec![]).push(reply_client.clone());

        if let Err(error) = self.notify_proxy_listen(setting_type).await {
            reply_client
                .reply(switchboard::Payload::Action(switchboard::Action::Response(Err(error))))
                .send();
        } else {
            reply_client.acknowledge().await;
        }
    }

    async fn remove_setting_listener(
        &mut self,
        setting_type: SettingType,
        mut client: switchboard::message::MessageClient,
    ) {
        if let Some(listeners) = self.listeners.get_mut(&setting_type) {
            if let Some(index) = listeners.iter().position(|x| *x == client) {
                listeners.remove(index);
                if let Err(error) = self.notify_proxy_listen(setting_type).await {
                    client
                        .reply(switchboard::Payload::Action(switchboard::Action::Response(Err(
                            error,
                        ))))
                        .send();
                }
            }
        }
        client.acknowledge().await;
    }

    fn process_action_request(
        &mut self,
        setting_type: SettingType,
        request: Request,
        reply_client: switchboard::message::MessageClient,
    ) -> Result<(), Error> {
        let core_messenger = self.core_messenger.clone();
        let action_id = self.get_next_action_id();

        let signature = match self.setting_proxies.entry(setting_type) {
            Entry::Vacant(_) => {
                reply_client
                    .reply(switchboard::Payload::Action(switchboard::Action::Response(Err(
                        Error::UnhandledType(setting_type),
                    ))))
                    .send();
                return Err(Error::UnhandledType(setting_type));
            }
            Entry::Occupied(occupied) => occupied.get().clone(),
        };

        let mut receptor = core_messenger
            .message(
                core::Payload::Action(SettingAction {
                    id: action_id,
                    setting_type,
                    data: SettingActionData::Request(request),
                }),
                Audience::Messenger(signature),
            )
            .send();

        fasync::Task::spawn(async move {
            while let Some(message_event) = receptor.next().await {
                // Wait for response
                if let MessageEvent::Message(
                    core::Payload::Event(SettingEvent::Response(_id, response)),
                    _,
                ) = message_event
                {
                    reply_client
                        .reply(switchboard::Payload::Action(switchboard::Action::Response(
                            response.map_err(|controller_err| controller_err.into()),
                        )))
                        .send();
                    return;
                }
            }
        })
        .detach();

        Ok(())
    }

    async fn notify_proxy_listen(&mut self, setting_type: SettingType) -> Result<(), Error> {
        if !self.setting_proxies.contains_key(&setting_type) {
            return Err(Error::UnhandledType(setting_type));
        }

        let action_id = self.get_next_action_id();
        let listener_count = self.listeners.get(&setting_type).map_or(0, |x| x.len());

        let signature = match self.setting_proxies.entry(setting_type) {
            Entry::Vacant(_) => return Err(Error::UnhandledType(setting_type)),
            Entry::Occupied(occupied) => occupied.get().clone(),
        };

        self.core_messenger
            .message(
                core::Payload::Action(SettingAction {
                    id: action_id,
                    setting_type,
                    data: SettingActionData::Listen(listener_count as u64),
                }),
                Audience::Messenger(signature),
            )
            .send()
            .wait_for_acknowledge()
            .await
            .ok();

        Ok(())
    }

    fn notify_listeners(&self, setting_type: &SettingType, setting_info: SettingInfo) {
        if let Some(clients) = self.listeners.get(setting_type) {
            for client in clients {
                client
                    .reply(switchboard::Payload::Listen(switchboard::Listen::Update(
                        setting_info.clone(),
                    )))
                    .send()
                    .ack();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::base::UnknownInfo;
    use crate::internal::core;
    use crate::message::base::Audience;

    async fn retrieve_and_verify_action(
        receptor: &mut core::message::Receptor,
        setting_type: SettingType,
        setting_data: SettingActionData,
    ) -> (core::message::MessageClient, SettingAction) {
        while let Some(event) = receptor.next().await {
            match event {
                MessageEvent::Message(core::Payload::Action(action), client) => {
                    assert_eq!(setting_type, action.setting_type);
                    assert_eq!(setting_data, action.data);
                    return (client, action);
                }
                _ => {
                    // ignore other messages
                }
            }
        }

        panic!("expected Payload::Action");
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_request() {
        let messenger_factory = core::message::create_hub();
        let switchboard_factory = switchboard::message::create_hub();

        // Create proxy endpoint.
        let mut proxy_receptor = messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("receptor should be created")
            .1;

        assert!(SwitchboardBuilder::create()
            .core_messenger_factory(messenger_factory.clone())
            .switchboard_messenger_factory(switchboard_factory.clone())
            .add_setting_proxy(SettingType::Unknown, proxy_receptor.get_signature())
            .build()
            .await
            .is_ok());

        // Create client.
        let (messenger, _) = switchboard_factory.create(MessengerType::Unbound).await.unwrap();

        // Send request.
        let mut message_receptor = messenger
            .message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    SettingType::Unknown,
                    Request::Get,
                )),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        // Ensure request is received.
        let (client, action) = retrieve_and_verify_action(
            &mut proxy_receptor,
            SettingType::Unknown,
            SettingActionData::Request(Request::Get),
        )
        .await;

        client.reply(core::Payload::Event(SettingEvent::Response(action.id, Ok(None)))).send();

        // Ensure response is received.
        let (response, _) = message_receptor.next_payload().await.unwrap();

        if let switchboard::Payload::Action(switchboard::Action::Response(result)) = response {
            assert!(result.is_ok());
        } else {
            panic!("should have received a switchboard::Action::Response");
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_unhandled_request() {
        let messenger_factory = core::message::create_hub();
        let switchboard_factory = switchboard::message::create_hub();

        assert!(SwitchboardBuilder::create()
            .core_messenger_factory(messenger_factory.clone())
            .switchboard_messenger_factory(switchboard_factory.clone())
            .build()
            .await
            .is_ok());

        // Create client.
        let (messenger, _) = switchboard_factory.create(MessengerType::Unbound).await.unwrap();

        // Send request.
        let mut message_receptor = messenger
            .message(
                switchboard::Payload::Action(switchboard::Action::Request(
                    SettingType::Unknown,
                    Request::Get,
                )),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        // Ensure response is received.
        let (response, _) = message_receptor.next_payload().await.unwrap();

        assert!(
            matches!(response, switchboard::Payload::Action(switchboard::Action::Response(Err(_)))),
            "should have received a switchboard::Action::Response"
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_listen() {
        let messenger_factory = core::message::create_hub();
        let switchboard_factory = switchboard::message::create_hub();

        // Create proxy endpoint.
        let mut receptor = messenger_factory
            .create(MessengerType::Unbound)
            .await
            .expect("receptor should be created")
            .1;

        assert!(SwitchboardBuilder::create()
            .core_messenger_factory(messenger_factory.clone())
            .add_setting_proxy(SettingType::Unknown, receptor.get_signature())
            .switchboard_messenger_factory(switchboard_factory.clone())
            .build()
            .await
            .is_ok());
        let setting_type = SettingType::Unknown;

        // Create client.
        let (messenger, _) = switchboard_factory.create(MessengerType::Unbound).await.unwrap();

        // Register first listener and verify count.
        {
            let _ = messenger
                .message(
                    switchboard::Payload::Listen(switchboard::Listen::Request(
                        SettingType::Unknown,
                    )),
                    Audience::Address(switchboard::Address::Switchboard),
                )
                .send();

            let _ = retrieve_and_verify_action(
                &mut receptor,
                setting_type,
                SettingActionData::Listen(1),
            )
            .await;
        }

        let _ =
            retrieve_and_verify_action(&mut receptor, setting_type, SettingActionData::Listen(0))
                .await;
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_notify() {
        let messenger_factory = core::message::create_hub();
        let switchboard_factory = switchboard::message::create_hub();

        // Create proxy endpoint.
        let (proxy_messenger, mut proxy_receptor) =
            messenger_factory.create(MessengerType::Unbound).await.unwrap();

        assert!(SwitchboardBuilder::create()
            .core_messenger_factory(messenger_factory.clone())
            .add_setting_proxy(SettingType::Unknown, proxy_receptor.get_signature())
            .switchboard_messenger_factory(switchboard_factory.clone())
            .build()
            .await
            .is_ok());
        let setting_type = SettingType::Unknown;

        // Create client.
        let (messenger_1, _) = switchboard_factory.create(MessengerType::Unbound).await.unwrap();

        let mut receptor_1 = messenger_1
            .message(
                switchboard::Payload::Listen(switchboard::Listen::Request(SettingType::Unknown)),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        let _ = retrieve_and_verify_action(
            &mut proxy_receptor,
            setting_type,
            SettingActionData::Listen(1),
        )
        .await;

        // Create client.
        let (messenger_2, _) = switchboard_factory.create(MessengerType::Unbound).await.unwrap();

        let mut receptor_2 = messenger_2
            .message(
                switchboard::Payload::Listen(switchboard::Listen::Request(SettingType::Unknown)),
                Audience::Address(switchboard::Address::Switchboard),
            )
            .send();

        let _ = retrieve_and_verify_action(
            &mut proxy_receptor,
            setting_type,
            SettingActionData::Listen(2),
        )
        .await;

        proxy_messenger
            .message(
                core::Payload::Event(SettingEvent::Changed(SettingInfo::Unknown(UnknownInfo(
                    true,
                )))),
                Audience::Address(core::Address::Switchboard),
            )
            .send();

        // Ensure both listeners receive notifications.
        assert!(matches!(
            receptor_1.next_payload().await.expect("update should be present").0,
            switchboard::Payload::Listen(switchboard::Listen::Update(..))
        ));
        assert!(matches!(
            receptor_2.next_payload().await.expect("update should be present").0,
            switchboard::Payload::Listen(switchboard::Listen::Update(..))
        ));
    }
}
