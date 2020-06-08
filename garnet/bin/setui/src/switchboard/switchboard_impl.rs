// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::clock;
use crate::internal::core;
use crate::message::base::{Audience, MessageEvent, MessengerType};
use crate::switchboard::base::{
    ListenCallback, ListenSession, SettingAction, SettingActionData, SettingEvent, SettingRequest,
    SettingRequestResponder, SettingType, Switchboard, SwitchboardClient, SwitchboardHandle,
};

use anyhow::{format_err, Error};
use fuchsia_async as fasync;
use fuchsia_inspect::{self as inspect, component, Property};
use fuchsia_inspect_derive::{Inspect, WithInspect};
use futures::channel::mpsc::UnboundedSender;
use futures::lock::Mutex;
use futures::stream::StreamExt;
use futures::FutureExt;
use std::collections::{HashMap, VecDeque};
use std::sync::Arc;
use std::time::{Duration, SystemTime};

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

/// Information about a switchboard setting to be written to inspect.
#[derive(Inspect)]
struct SettingTypeInfo {
    /// Last requests for inspect to save. Number of requests is defined by INSPECT_REQUESTS_COUNT.
    #[inspect(skip)]
    last_requests: VecDeque<RequestInfo>,

    /// Node of this info.
    inspect_node: inspect::Node,
}

impl SettingTypeInfo {
    fn new() -> Self {
        Self {
            last_requests: VecDeque::with_capacity(INSPECT_REQUESTS_COUNT),
            inspect_node: inspect::Node::default(),
        }
    }
}

/// Information about a switchboard request to be written to inspect.
///
/// Inspect nodes and properties are not used, but need to be held as they're deleted from inspect
/// once they go out of scope.
#[derive(Inspect)]
struct RequestInfo {
    /// Incrementing count for each request.
    #[inspect(skip)]
    count: u64,

    /// Debug string representation of this SettingRequest.
    request: inspect::StringProperty,

    /// Milliseconds since switchboard creation that this request arrived.
    timestamp: inspect::StringProperty,

    /// Node of this info.
    inspect_node: inspect::Node,
}

impl RequestInfo {
    fn new(count: u64) -> Self {
        Self {
            count,
            request: inspect::StringProperty::default(),
            timestamp: inspect::StringProperty::default(),
            inspect_node: inspect::Node::default(),
        }
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

pub struct SwitchboardBuilder {
    registry_messenger_factory: Option<core::message::Factory>,
    inspect_node: Option<inspect::Node>,
}

impl SwitchboardBuilder {
    pub fn create() -> Self {
        SwitchboardBuilder { registry_messenger_factory: None, inspect_node: None }
    }

    pub fn registry_messenger_factory(mut self, factory: core::message::Factory) -> Self {
        self.registry_messenger_factory = Some(factory);
        self
    }

    pub fn inspect_node(mut self, node: inspect::Node) -> Self {
        self.inspect_node = Some(node);
        self
    }

    pub async fn build(self) -> Result<SwitchboardClient, Error> {
        let registry_messenger_factory = if let Some(factory) = self.registry_messenger_factory {
            factory
        } else {
            core::message::create_hub()
        };
        let inspect_node = if let Some(node) = self.inspect_node {
            node
        } else {
            component::inspector().root().create_child("switchboard")
        };

        SwitchboardImpl::create(registry_messenger_factory, inspect_node).await
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
    registry_messenger: core::message::Messenger,
    /// Last requests for inspect to save.
    last_requests: HashMap<SettingType, SettingTypeInfo>,
    /// Inspect node to record last requests to.
    inspect_node: fuchsia_inspect::Node,
}

impl SwitchboardImpl {
    /// Creates a new SwitchboardImpl, which will return the instance along with
    /// a sender to provide events in response to the actions sent.
    ///
    /// Requests will be recorded to the given inspect node.
    async fn create(
        registry_messenger_factory: core::message::Factory,
        inspect_node: inspect::Node,
    ) -> Result<SwitchboardClient, Error> {
        let (cancel_listen_tx, mut cancel_listen_rx) =
            futures::channel::mpsc::unbounded::<ListenSessionInfo>();
        let messenger_result = registry_messenger_factory
            .create(MessengerType::Addressable(core::Address::Switchboard))
            .await;

        if let Err(error) = messenger_result {
            return Err(Error::new(error));
        }

        let (registry_messenger, mut receptor) = messenger_result.unwrap();

        let switchboard = Arc::new(Mutex::new(Self {
            next_session_id: 0,
            next_action_id: 0,
            listen_cancellation_sender: cancel_listen_tx,
            listeners: HashMap::new(),
            registry_messenger: registry_messenger,
            last_requests: HashMap::new(),
            inspect_node: inspect_node,
        }));

        let switchboard_clone = switchboard.clone();

        fasync::spawn(async move {
            loop {
                let mut message_receptor = receptor.next().fuse();
                let mut cancel_receptor = cancel_listen_rx.next().fuse();
                futures::select! {
                    // Invoked when there is a new message from the registry.
                    message_event = message_receptor => {
                        if let Some(MessageEvent::Message(core::Payload::Event(event), _)) = message_event {
                            switchboard_clone.lock().await.process_event(event);
                        }
                    }
                    // Invoked when a listen session has ended.
                    cancel_event = cancel_receptor => {
                        if let Some(info) = cancel_event {
                            switchboard_clone.lock().await.stop_listening(info);
                        }
                    }
                }
            }
        });

        return Ok(SwitchboardClient::new(&(switchboard as SwitchboardHandle)));
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
                        core::Payload::Action(SettingAction {
                            id: action_id,
                            setting_type: session_info.setting_type,
                            data: SettingActionData::Listen(session_infos.len() as u64),
                        }),
                        Audience::Address(core::Address::Registry),
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
        let inspect_node = &self.inspect_node;
        let setting_type_info = self.last_requests.entry(setting_type).or_insert_with(|| {
            SettingTypeInfo::new()
                .with_inspect(&inspect_node, format!("{:?}", setting_type))
                // `with_inspect` will only return an error on types with interior mutability.
                // Since none are used here, this should be fine.
                .unwrap()
        });

        let last_requests = &mut setting_type_info.last_requests;
        if last_requests.len() >= INSPECT_REQUESTS_COUNT {
            last_requests.pop_back();
        }

        let count = last_requests.front().map(|req| req.count + 1).unwrap_or(0);
        let timestamp = clock::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .as_ref()
            .map(Duration::as_millis)
            .unwrap_or(0);
        // std::u64::MAX maxes out at 20 digits.
        if let Ok(request_info) = RequestInfo::new(count)
            .with_inspect(&setting_type_info.inspect_node, format!("{:020}", count))
        {
            request_info.request.set(&format!("{:?}", request));
            request_info.timestamp.set(&timestamp.to_string());
            last_requests.push_front(request_info);
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

        self.record_request(setting_type.clone(), request.clone());

        fasync::spawn(async move {
            let mut receptor = messenger
                .message(
                    core::Payload::Action(SettingAction {
                        id: action_id,
                        setting_type,
                        data: SettingActionData::Request(request),
                    }),
                    Audience::Address(core::Address::Registry),
                )
                .send();

            while let Some(message_event) = receptor.next().await {
                // Wait for response
                if let MessageEvent::Message(
                    core::Payload::Event(SettingEvent::Response(_id, response)),
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
                    core::Payload::Action(SettingAction {
                        id: action_id,
                        setting_type,
                        data: SettingActionData::Listen(listeners.len() as u64),
                    }),
                    Audience::Address(core::Address::Registry),
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
    use crate::internal::core;
    use crate::message::base::Audience;
    use crate::message::receptor::Receptor;
    use crate::switchboard::intl_types::{IntlInfo, LocaleId, TemperatureUnit};
    use fuchsia_inspect::{
        assert_inspect_tree,
        testing::{AnyProperty, TreeAssertion},
    };

    async fn retrieve_and_verify_action(
        receptor: &mut Receptor<core::Payload, core::Address>,
        setting_type: SettingType,
        setting_data: SettingActionData,
    ) -> (core::message::Client, SettingAction) {
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

    /// Exercises locking behavior around the Switchboard in the
    /// SwitchboardClient. Consumers should be able to call client methods and
    /// use the return values without holding onto the Switchboard.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_client_access() {
        let switchboard_client = SwitchboardBuilder::create().build().await.unwrap();

        // Match holds the return value in a temporary location, preventing
        // resources from going out of scope and being freed.
        match switchboard_client.request(SettingType::Unknown, SettingRequest::Get).await {
            Ok(_) => {
                switchboard_client.request(SettingType::Unknown, SettingRequest::Get).await.ok();
            }
            Err(_) => {
                switchboard_client.request(SettingType::Unknown, SettingRequest::Get).await.ok();
            }
        }

        // Resources should not be held beyond the above calls
        switchboard_client.request(SettingType::Unknown, SettingRequest::Get).await.ok();
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_request() {
        let messenger_factory = core::message::create_hub();
        let switchboard_client = SwitchboardBuilder::create()
            .registry_messenger_factory(messenger_factory.clone())
            .build()
            .await
            .unwrap();
        // Create registry endpoint.
        let (_, mut receptor) = messenger_factory
            .create(MessengerType::Addressable(core::Address::Registry))
            .await
            .unwrap();

        // Send request.
        let result = switchboard_client.request(SettingType::Unknown, SettingRequest::Get).await;
        assert!(result.is_ok());
        let response_rx = result.unwrap();

        // Ensure request is received.
        let (client, action) = retrieve_and_verify_action(
            &mut receptor,
            SettingType::Unknown,
            SettingActionData::Request(SettingRequest::Get),
        )
        .await;

        client.reply(core::Payload::Event(SettingEvent::Response(action.id, Ok(None)))).send();

        // Ensure response is received.
        let response = response_rx.await.unwrap();
        assert!(response.is_ok());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_listen() {
        let messenger_factory = core::message::create_hub();
        let switchboard_client = SwitchboardBuilder::create()
            .registry_messenger_factory(messenger_factory.clone())
            .build()
            .await
            .unwrap();
        let setting_type = SettingType::Unknown;

        // Create registry endpoint.
        let (_, mut receptor) = messenger_factory
            .create(MessengerType::Addressable(core::Address::Registry))
            .await
            .unwrap();

        // Register first listener and verify count.
        let (notify_tx1, _notify_rx1) = futures::channel::mpsc::unbounded::<SettingType>();
        let listen_result = switchboard_client
            .listen(
                setting_type,
                Arc::new(move |setting| {
                    notify_tx1.unbounded_send(setting).ok();
                }),
            )
            .await;

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
        let messenger_factory = core::message::create_hub();
        let switchboard_client = SwitchboardBuilder::create()
            .registry_messenger_factory(messenger_factory.clone())
            .build()
            .await
            .unwrap();
        let setting_type = SettingType::Unknown;

        // Create registry endpoint.
        let (messenger, mut receptor) = messenger_factory
            .create(MessengerType::Addressable(core::Address::Registry))
            .await
            .unwrap();

        // Register first listener and verify count.
        let (notify_tx1, mut notify_rx1) = futures::channel::mpsc::unbounded::<SettingType>();
        let result_1 = switchboard_client
            .listen(
                setting_type,
                Arc::new(move |setting_type| {
                    notify_tx1.unbounded_send(setting_type).ok();
                }),
            )
            .await;
        assert!(result_1.is_ok());

        let _ =
            retrieve_and_verify_action(&mut receptor, setting_type, SettingActionData::Listen(1))
                .await;

        // Register second listener and verify count.
        let (notify_tx2, mut notify_rx2) = futures::channel::mpsc::unbounded::<SettingType>();
        let result_2 = switchboard_client
            .listen(
                setting_type,
                Arc::new(move |setting_type| {
                    notify_tx2.unbounded_send(setting_type).ok();
                }),
            )
            .await;
        assert!(result_2.is_ok());

        let _ =
            retrieve_and_verify_action(&mut receptor, setting_type, SettingActionData::Listen(2))
                .await;

        messenger
            .message(
                core::Payload::Event(SettingEvent::Changed(setting_type)),
                Audience::Address(core::Address::Switchboard),
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

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_inspect() {
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let switchboard_client =
            SwitchboardBuilder::create().inspect_node(inspect_node).build().await.unwrap();

        // Send a few requests to make sure they get written to inspect properly.
        switchboard_client
            .request(SettingType::Display, SettingRequest::SetAutoBrightness(false))
            .await
            .ok();
        switchboard_client
            .request(SettingType::Display, SettingRequest::SetAutoBrightness(false))
            .await
            .ok();

        switchboard_client
            .request(
                SettingType::Intl,
                SettingRequest::SetIntlInfo(IntlInfo {
                    locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                    temperature_unit: Some(TemperatureUnit::Celsius),
                    time_zone_id: Some("UTC".to_string()),
                    hour_cycle: None,
                }),
            )
            .await
            .ok();

        assert_inspect_tree!(inspector, root: {
            switchboard: {
                "Display": {
                    "00000000000000000000": {
                        request: "SetAutoBrightness(false)",
                        timestamp: "0",
                    },
                    "00000000000000000001": {
                        request: "SetAutoBrightness(false)",
                        timestamp: "0",
                    },
                },
                "Intl": {
                    "00000000000000000000": {
                        request: "SetIntlInfo(IntlInfo { locales: Some([LocaleId { id: \"en-US\" }]), temperature_unit: Some(Celsius), time_zone_id: Some(\"UTC\"), hour_cycle: None })",
                        timestamp: "0",
                    }
                }
            }
        });
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn inspect_queue_test() {
        clock::mock::set(SystemTime::UNIX_EPOCH);

        let inspector = inspect::Inspector::new();
        let inspect_node = inspector.root().create_child("switchboard");
        let switchboard_client =
            SwitchboardBuilder::create().inspect_node(inspect_node).build().await.unwrap();

        // Send one request of a different type to show the queue doesn't wipe out other setting types
        switchboard_client
            .request(
                SettingType::Intl,
                SettingRequest::SetIntlInfo(IntlInfo {
                    locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                    temperature_unit: Some(TemperatureUnit::Celsius),
                    time_zone_id: Some("UTC".to_string()),
                    hour_cycle: None,
                }),
            )
            .await
            .ok();

        // Send one more than the max requests to make sure they get pushed off the end of the queue
        for i in 0..26u8 {
            println!("{}", i);
            switchboard_client
                .request(SettingType::Display, SettingRequest::SetAutoBrightness(false))
                .await
                .ok();
        }

        // ensures we have 25 items and that the queue dropped the earliest one when hitting the limit
        fn display_subtree_assertion() -> TreeAssertion {
            let mut tree_assertion = TreeAssertion::new("Display", true);
            for i in 1..26 {
                tree_assertion
                    .add_child_assertion(TreeAssertion::new(&format!("{:020}", i), false));
            }
            tree_assertion
        };

        assert_inspect_tree!(inspector, root: {
            switchboard: {
                display_subtree_assertion(),
                "Intl": {
                    "00000000000000000000": {
                        request: AnyProperty,
                        timestamp: "0",
                    }
                }
            }
        });
    }
}
