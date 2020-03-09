// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::internal::core::{
    Address, MessageHubHandle as RegistryMessageHubHandle, Messenger as RegistryMessenger, Payload,
};
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::switchboard::base::*;

use anyhow::{format_err, Error};

use futures::channel::mpsc::UnboundedSender;
use futures::executor::block_on;
use futures::lock::Mutex;
use std::collections::HashMap;
use std::sync::Arc;

use fuchsia_async as fasync;
use futures::stream::StreamExt;

type ListenerMap = HashMap<SettingType, Vec<ListenSessionInfo>>;

/// Minimal data necessary to uniquely identify and interact with a listen
/// session.
#[derive(Clone)]
struct ListenSessionInfo {
    session_id: u64,

    /// Setting type listening to
    setting_type: SettingType,

    callback: ListenCallback,
}

impl PartialEq for ListenSessionInfo {
    fn eq(&self, other: &Self) -> bool {
        // We cannot derive PartialEq as UnboundedSender does not implement it.
        self.session_id == other.session_id && self.setting_type == other.setting_type
    }
}

/// Wrapper around ListenSessioninfo that provides cancellation ability as a
/// ListenSession.
struct ListenSessionImpl {
    info: ListenSessionInfo,

    /// Sender to invoke cancellation on. Sends the listener associated with
    /// this session.
    cancellation_sender: UnboundedSender<ListenSessionInfo>,

    closed: bool,
}

impl ListenSessionImpl {
    fn new(
        info: ListenSessionInfo,
        cancellation_sender: UnboundedSender<ListenSessionInfo>,
    ) -> Self {
        Self { info: info, cancellation_sender: cancellation_sender, closed: false }
    }
}

impl ListenSession for ListenSessionImpl {
    fn close(&mut self) {
        if self.closed {
            return;
        }

        let info_clone = self.info.clone();
        self.cancellation_sender.unbounded_send(info_clone).ok();
        self.closed = true;
    }
}

impl Drop for ListenSessionImpl {
    fn drop(&mut self) {
        self.close();
    }
}

pub struct SwitchboardImpl {
    /// Next available session id.
    next_session_id: u64,
    /// Next available action id.
    next_action_id: u64,
    /// Acquired during construction - passed during listen to allow callback
    /// for canceling listen.
    listen_cancellation_sender: UnboundedSender<ListenSessionInfo>,
    /// mapping of listeners for changes
    listeners: ListenerMap,
    /// registry messenger
    registry_messenger: RegistryMessenger,
}

impl SwitchboardImpl {
    /// Creates a new SwitchboardImpl, which will return the instance along with
    /// a sender to provide events in response to the actions sent.
    pub fn create(registry_message_hub: RegistryMessageHubHandle) -> Arc<Mutex<SwitchboardImpl>> {
        let (cancel_listen_tx, mut cancel_listen_rx) =
            futures::channel::mpsc::unbounded::<ListenSessionInfo>();

        let (registry_messenger, mut receptor) = block_on(registry_message_hub.lock())
            .create_messenger(MessengerType::Addressable(Address::Switchboard));

        let switchboard = Arc::new(Mutex::new(Self {
            next_session_id: 0,
            next_action_id: 0,
            listen_cancellation_sender: cancel_listen_tx,
            listeners: HashMap::new(),
            registry_messenger: registry_messenger,
        }));

        {
            let switchboard_clone = switchboard.clone();
            fasync::spawn(async move {
                while let Ok(message_event) = receptor.watch().await {
                    // Wait for response
                    if let MessageEvent::Message(Payload::Event(event), _) = message_event {
                        switchboard_clone.lock().await.process_event(event);
                    }
                }
            });
        }

        {
            let switchboard_clone = switchboard.clone();
            fasync::spawn(async move {
                while let Some(info) = cancel_listen_rx.next().await {
                    switchboard_clone.lock().await.stop_listening(info);
                }
            });
        }

        return switchboard;
    }

    pub fn get_next_action_id(&mut self) -> u64 {
        let return_id = self.next_action_id;
        self.next_action_id += 1;
        return return_id;
    }

    fn process_event(&mut self, input: SettingEvent) {
        match input {
            SettingEvent::Changed(setting_type) => {
                self.notify_listeners(setting_type);
            }
            _ => {}
        }
    }

    fn stop_listening(&mut self, session_info: ListenSessionInfo) {
        let action_id = self.get_next_action_id();

        if let Some(session_infos) = self.listeners.get_mut(&session_info.setting_type) {
            // FIXME: use `Vec::remove_item` upon stabilization
            let listener_to_remove =
                session_infos.iter().enumerate().find(|(_i, elem)| **elem == session_info);
            if let Some((i, _elem)) = listener_to_remove {
                session_infos.remove(i);

                let _ = self
                    .registry_messenger
                    .message(
                        Payload::Action(SettingAction {
                            id: action_id,
                            setting_type: session_info.setting_type,
                            data: SettingActionData::Listen(session_infos.len() as u64),
                        }),
                        Audience::Address(Address::Registry),
                    )
                    .send();
            }
        }
    }

    fn notify_listeners(&self, setting_type: SettingType) {
        if let Some(session_infos) = self.listeners.get(&setting_type) {
            for info in session_infos {
                (info.callback)(setting_type);
            }
        }
    }
}

impl Switchboard for SwitchboardImpl {
    fn request(
        &mut self,
        setting_type: SettingType,
        request: SettingRequest,
        callback: SettingRequestResponder,
    ) -> Result<(), Error> {
        let messenger = self.registry_messenger.clone();
        let action_id = self.get_next_action_id();

        fasync::spawn(async move {
            let mut receptor = messenger
                .message(
                    Payload::Action(SettingAction {
                        id: action_id,
                        setting_type,
                        data: SettingActionData::Request(request),
                    }),
                    Audience::Address(Address::Registry),
                )
                .send();

            while let Ok(message_event) = receptor.watch().await {
                // Wait for response
                if let MessageEvent::Message(
                    Payload::Event(SettingEvent::Response(_id, response)),
                    _,
                ) = message_event
                {
                    callback.send(response).ok();
                    return;
                }
            }
        });

        return Ok(());
    }

    fn listen(
        &mut self,
        setting_type: SettingType,
        listener: ListenCallback,
    ) -> Result<Box<dyn ListenSession + Send + Sync>, Error> {
        let action_id = self.get_next_action_id();

        if !self.listeners.contains_key(&setting_type) {
            self.listeners.insert(setting_type, vec![]);
        }

        if let Some(listeners) = self.listeners.get_mut(&setting_type) {
            let info = ListenSessionInfo {
                session_id: self.next_session_id,
                setting_type: setting_type,
                callback: listener,
            };

            self.next_session_id += 1;

            listeners.push(info.clone());

            let _ = self
                .registry_messenger
                .message(
                    Payload::Action(SettingAction {
                        id: action_id,
                        setting_type,
                        data: SettingActionData::Listen(listeners.len() as u64),
                    }),
                    Audience::Address(Address::Registry),
                )
                .send();

            return Ok(Box::new(ListenSessionImpl::new(
                info,
                self.listen_cancellation_sender.clone(),
            )));
        }

        return Err(format_err!("invalid error"));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::internal::core::{
        create_message_hub as create_registry_hub, Address, MessageClient, Payload,
    };
    use crate::message::base::Audience;
    use crate::message::receptor::Receptor;

    async fn retrieve_and_verify_action(
        receptor: &mut Receptor<Payload, Address>,
        setting_type: SettingType,
        setting_data: SettingActionData,
    ) -> (MessageClient, SettingAction) {
        while let Ok(event) = receptor.watch().await {
            match event {
                MessageEvent::Message(Payload::Action(action), client) => {
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
        let message_hub = create_registry_hub();
        let switchboard = SwitchboardImpl::create(message_hub.clone());
        // Create registry endpoint
        let (_, mut receptor) = message_hub
            .lock()
            .await
            .create_messenger(MessengerType::Addressable(Address::Registry));

        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<SettingResponseResult>();

        // Send request
        assert!(switchboard
            .lock()
            .await
            .request(SettingType::Unknown, SettingRequest::Get, response_tx)
            .is_ok());

        // Ensure request is received.
        let (client, action) = retrieve_and_verify_action(
            &mut receptor,
            SettingType::Unknown,
            SettingActionData::Request(SettingRequest::Get),
        )
        .await;

        client.reply(Payload::Event(SettingEvent::Response(action.id, Ok(None)))).send();

        // Ensure response is received.
        let response = response_rx.await.unwrap();
        assert!(response.is_ok());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_listen() {
        let message_hub = create_registry_hub();
        let switchboard = SwitchboardImpl::create(message_hub.clone());
        let setting_type = SettingType::Unknown;

        // Create registry endpoint
        let (_, mut receptor) = message_hub
            .lock()
            .await
            .create_messenger(MessengerType::Addressable(Address::Registry));

        // Register first listener and verify count.
        let (notify_tx1, _notify_rx1) = futures::channel::mpsc::unbounded::<SettingType>();
        let listen_result = switchboard.lock().await.listen(
            setting_type,
            Arc::new(move |setting| {
                notify_tx1.unbounded_send(setting).ok();
            }),
        );

        assert!(listen_result.is_ok());
        let _ =
            retrieve_and_verify_action(&mut receptor, setting_type, SettingActionData::Listen(1))
                .await;

        // Unregister and verify count.
        if let Ok(mut listen_session) = listen_result {
            listen_session.close();
        } else {
            panic!("should have a session");
        }

        let _ =
            retrieve_and_verify_action(&mut receptor, setting_type, SettingActionData::Listen(0))
                .await;
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_notify() {
        let message_hub = create_registry_hub();
        let switchboard = SwitchboardImpl::create(message_hub.clone());
        let setting_type = SettingType::Unknown;

        // Create registry endpoint
        let (messenger, mut receptor) = message_hub
            .lock()
            .await
            .create_messenger(MessengerType::Addressable(Address::Registry));

        // Register first listener and verify count.
        let (notify_tx1, mut notify_rx1) = futures::channel::mpsc::unbounded::<SettingType>();
        let result_1 = switchboard.lock().await.listen(
            setting_type,
            Arc::new(move |setting_type| {
                notify_tx1.unbounded_send(setting_type).ok();
            }),
        );
        assert!(result_1.is_ok());

        let _ =
            retrieve_and_verify_action(&mut receptor, setting_type, SettingActionData::Listen(1))
                .await;

        // Register second listener and verify count
        let (notify_tx2, mut notify_rx2) = futures::channel::mpsc::unbounded::<SettingType>();
        let result_2 = switchboard.lock().await.listen(
            setting_type,
            Arc::new(move |setting_type| {
                notify_tx2.unbounded_send(setting_type).ok();
            }),
        );
        assert!(result_2.is_ok());

        let _ =
            retrieve_and_verify_action(&mut receptor, setting_type, SettingActionData::Listen(2))
                .await;

        messenger
            .message(
                Payload::Event(SettingEvent::Changed(setting_type)),
                Audience::Address(Address::Switchboard),
            )
            .send();

        // Ensure both listeners receive notifications.
        {
            let notification = notify_rx1.next().await.unwrap();
            assert_eq!(notification, setting_type);
        }
        {
            let notification = notify_rx2.next().await.unwrap();
            assert_eq!(notification, setting_type);
        }
    }
}
