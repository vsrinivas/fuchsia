// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::internal::core::{
    Address, MessengerClient as RegistryMessengerClient,
    MessengerFactory as RegistryMessengerFactory, Payload,
};
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::switchboard::base::*;

use anyhow::{format_err, Error};

use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::SystemTime;

use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component};
use futures::stream::StreamExt;
use std::collections::VecDeque;

type ListenerMap = HashMap<SettingType, Vec<ListenSessionInfo>>;

const INSPECT_REQUESTS_COUNT: usize = 25;

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

/// Information about a switchboard request to be written to inspect.
///
/// Inspect nodes and properties are not used, but need to be held as they're deleted from inspect
/// once they go out of scope.
struct RequestInfo {
    /// Incrementing count for each request.
    count: u64,

    /// Node of this info.
    _node: inspect::Node,

    /// Debug string representation of the SettingType of this request.
    _setting_type: inspect::StringProperty,

    /// Debug string representation of this SettingRequest.
    _request: inspect::StringProperty,

    /// Milliseconds since switchboard creation that this request arrived.
    _timestamp: inspect::StringProperty,
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
    registry_messenger_client: RegistryMessengerClient,
    /// Last requests for inspect to save. Number of requests is defined by INSPECT_REQUESTS_COUNT.
    last_requests: VecDeque<RequestInfo>,
    /// Inspect node to write last requests to.
    last_requests_node: fuchsia_inspect::Node,
    /// Time the switchboard was created, used for inspect timestamps.
    creation_time: SystemTime,
}

impl SwitchboardImpl {
    /// Creates a new SwitchboardImpl, which will return the instance along with
    /// a sender to provide events in response to the actions sent.
    pub async fn create(
        registry_messenger_factory: RegistryMessengerFactory,
    ) -> Result<Arc<Mutex<SwitchboardImpl>>, Error> {
        let (cancel_listen_tx, mut cancel_listen_rx) =
            futures::channel::mpsc::unbounded::<ListenSessionInfo>();
        let messenger_result = registry_messenger_factory
            .create(MessengerType::Addressable(Address::Switchboard))
            .await;

        if let Err(error) = messenger_result {
            return Err(Error::new(error));
        }

        let (registry_messenger_client, mut receptor) = messenger_result.unwrap();

        let inspector = component::inspector();
        let switchboard = Arc::new(Mutex::new(Self {
            next_session_id: 0,
            next_action_id: 0,
            listen_cancellation_sender: cancel_listen_tx,
            listeners: HashMap::new(),
            registry_messenger_client: registry_messenger_client,
            last_requests: VecDeque::with_capacity(INSPECT_REQUESTS_COUNT),
            last_requests_node: inspector.root().create_child("last_requests"),
            creation_time: SystemTime::now(),
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

        return Ok(switchboard);
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
                    .registry_messenger_client
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

    /// Write a request to inspect.
    fn record_request(&mut self, setting_type: SettingType, request: SettingRequest) {
        if self.last_requests.len() >= INSPECT_REQUESTS_COUNT {
            self.last_requests.pop_back();
        }

        let count = match self.last_requests.front() {
            Some(req) => req.count + 1,
            None => 0,
        };
        let timestamp = match self.creation_time.elapsed() {
            Ok(elapsed) => elapsed.as_millis(),
            Err(_) => 0,
        };
        let node = self.last_requests_node.create_child(format!("{:020}", count));
        let setting_property = node.create_string("setting_type", format!("{:?}", setting_type));
        let request_property = node.create_string("request", format!("{:?}", request));
        let timestamp = node.create_string("timestamp", timestamp.to_string());
        self.last_requests.push_front(RequestInfo {
            count,
            _node: node,
            _setting_type: setting_property,
            _request: request_property,
            _timestamp: timestamp,
        });
    }
}

impl Switchboard for SwitchboardImpl {
    fn request(
        &mut self,
        setting_type: SettingType,
        request: SettingRequest,
        callback: SettingRequestResponder,
    ) -> Result<(), Error> {
        let messenger = self.registry_messenger_client.clone();
        let action_id = self.get_next_action_id();

        self.record_request(setting_type.clone(), request.clone());

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
                .registry_messenger_client
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
        let messenger_factory = create_registry_hub();
        let switchboard = SwitchboardImpl::create(messenger_factory.clone()).await.unwrap();
        // Create registry endpoint
        let (_, mut receptor) =
            messenger_factory.create(MessengerType::Addressable(Address::Registry)).await.unwrap();

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
        let messenger_factory = create_registry_hub();
        let switchboard = SwitchboardImpl::create(messenger_factory.clone()).await.unwrap();
        let setting_type = SettingType::Unknown;

        // Create registry endpoint
        let (_, mut receptor) =
            messenger_factory.create(MessengerType::Addressable(Address::Registry)).await.unwrap();

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
        let messenger_factory = create_registry_hub();
        let switchboard = SwitchboardImpl::create(messenger_factory.clone()).await.unwrap();
        let setting_type = SettingType::Unknown;

        // Create registry endpoint
        let (messenger, mut receptor) =
            messenger_factory.create(MessengerType::Addressable(Address::Registry)).await.unwrap();

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
