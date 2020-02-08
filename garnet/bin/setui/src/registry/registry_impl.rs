// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::conduit::base::{ConduitData, ConduitHandle, ConduitSender};
use crate::registry::base::{Command, SettingHandler, SettingHandlerFactory, State};
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingResponseResult,
    SettingType,
};

use anyhow::{format_err, Context as _};
use fuchsia_async as fasync;

use futures::channel::mpsc::UnboundedSender;
use futures::executor::block_on;
use futures::lock::Mutex;
use futures::stream::StreamExt;
use futures::TryFutureExt;
use std::collections::HashMap;
use std::sync::Arc;

pub struct RegistryImpl {
    /// A mapping of setting types to senders, used to relay new commands.
    command_sender_map: HashMap<SettingType, UnboundedSender<Command>>,
    conduit_sender: ConduitSender,
    /// A sender handed as part of State::Listen to allow entities to provide
    /// back updates.
    /// TODO(SU-334): Investigate restricting the setting type a sender may
    /// specify to the one registered.
    notification_sender: UnboundedSender<SettingType>,
    /// The current types being listened in on.
    active_listeners: Vec<SettingType>,
    /// handler factory
    handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
}

impl RegistryImpl {
    /// Returns a handle to a RegistryImpl which is listening to SettingAction from the
    /// provided receiver and will send responses/updates on the given sender.
    pub fn create(
        handler_factory: Arc<Mutex<dyn SettingHandlerFactory + Send + Sync>>,
        conduit: ConduitHandle,
    ) -> Arc<Mutex<RegistryImpl>> {
        let (notification_tx, mut notification_rx) =
            futures::channel::mpsc::unbounded::<SettingType>();

        let (conduit_tx, mut conduit_rx) = block_on(conduit.lock()).create_waypoint();

        // We must create handle here rather than return back the value as we
        // reference the registry in the async tasks below.
        let registry = Arc::new(Mutex::new(Self {
            command_sender_map: HashMap::new(),
            conduit_sender: conduit_tx,
            notification_sender: notification_tx,
            active_listeners: vec![],
            handler_factory: handler_factory,
        }));

        // An async task is spawned here to listen to the SettingAction for new
        // directives. Note that the receiver is the one provided in the arguments.
        {
            let registry_clone = registry.clone();
            fasync::spawn(
                async move {
                    while let Some(conduit_data) = conduit_rx.next().await {
                        if let ConduitData::Action(action) = conduit_data {
                            registry_clone.lock().await.process_action(action).await;
                        }
                    }
                    Ok(())
                }
                .unwrap_or_else(|_e: anyhow::Error| {}),
            );
        }

        // An async task is spawned here to listen for notifications. The receiver
        // handles notifications from all sources.
        {
            let registry_clone = registry.clone();
            fasync::spawn(
                async move {
                    while let Some(setting_type) = notification_rx.next().await {
                        registry_clone.lock().await.notify(setting_type);
                    }
                    Ok(())
                }
                .unwrap_or_else(|_e: anyhow::Error| {}),
            );
        }

        return registry;
    }

    /// Interpret action from switchboard into registry actions.
    async fn process_action(&mut self, action: SettingAction) {
        match action.data {
            SettingActionData::Request(request) => {
                self.process_request(action.id, action.setting_type, request).await;
            }
            SettingActionData::Listen(size) => {
                self.process_listen(action.setting_type, size).await;
            }
        }
    }

    /// Returns an existing handler for a given setting type. If such handler
    /// does not exist, this method will attempt to generate one, returning
    /// either the successfully created handler or None otherwise.
    async fn get_handler(&mut self, setting_type: SettingType) -> Option<SettingHandler> {
        let existing_handler = self.command_sender_map.get(&setting_type);

        if let Some(handler) = existing_handler {
            return Some(handler.clone());
        }

        let new_handler = self.handler_factory.lock().await.generate(setting_type);
        if let Some(handler) = new_handler {
            self.command_sender_map.insert(setting_type, handler.clone());
            return Some(handler);
        }

        return None;
    }

    /// Notifies proper sink in the case the notification listener count is
    /// non-zero and we aren't already listening for changes to the type or there
    /// are no more listeners and we are actively listening.
    async fn process_listen(&mut self, setting_type: SettingType, size: u64) {
        let candidate = self.get_handler(setting_type).await;

        if candidate.is_none() {
            return;
        }

        let sender = candidate.unwrap();

        let listening = self.active_listeners.contains(&setting_type);

        if size == 0 && listening {
            // FIXME: use `Vec::remove_item` upon stabilization
            let listener_to_remove =
                self.active_listeners.iter().enumerate().find(|(_i, elem)| **elem == setting_type);
            if let Some((i, _elem)) = listener_to_remove {
                self.active_listeners.remove(i);
            }
            sender.unbounded_send(Command::ChangeState(State::EndListen)).ok();
        } else if size > 0 && !listening {
            self.active_listeners.push(setting_type);
            sender
                .unbounded_send(Command::ChangeState(State::Listen(
                    self.notification_sender.clone(),
                )))
                .ok();
        }
    }

    /// Called by the receiver task when a sink has reported a change to its
    /// setting type.
    fn notify(&self, setting_type: SettingType) {
        // Only return updates for types actively listened on.
        if self.active_listeners.contains(&setting_type) {
            self.conduit_sender.send(ConduitData::Event(SettingEvent::Changed(setting_type)));
        }
    }

    /// Forwards request to proper sink. A new task is spawned in order to receive
    /// the response. If no sink is available, an error is immediately reported
    /// back.
    async fn process_request(
        &mut self,
        id: u64,
        setting_type: SettingType,
        request: SettingRequest,
    ) {
        let candidate = self.get_handler(setting_type).await;
        match candidate {
            None => {
                self.conduit_sender.send(ConduitData::Event(SettingEvent::Response(
                    id,
                    Err(format_err!("no handler for requested type")),
                )));
            }
            Some(command_sender) => {
                let (responder, receiver) =
                    futures::channel::oneshot::channel::<SettingResponseResult>();
                let sender_clone = self.conduit_sender.clone();
                let error_sender_clone = self.conduit_sender.clone();
                fasync::spawn(
                    async move {
                        let response =
                            receiver.await.context("getting response from controller")?;
                        sender_clone.send(ConduitData::Event(SettingEvent::Response(id, response)));

                        Ok(())
                    }
                    .unwrap_or_else(move |e: anyhow::Error| {
                        error_sender_clone
                            .send(ConduitData::Event(SettingEvent::Response(id, Err(e))));
                    }),
                );

                command_sender.unbounded_send(Command::HandleRequest(request, responder)).ok();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::conduit::base::Conduit;
    use crate::conduit::conduit_impl::ConduitImpl;
    use crate::registry::base::SettingHandler;

    struct FakeFactory {
        handlers: HashMap<SettingType, SettingHandler>,
        request_counts: HashMap<SettingType, u64>,
    }

    impl FakeFactory {
        pub fn new() -> Self {
            FakeFactory { handlers: HashMap::new(), request_counts: HashMap::new() }
        }

        pub fn register(&mut self, setting_type: SettingType, handler: SettingHandler) {
            self.handlers.insert(setting_type, handler);
        }

        pub fn get_request_count(&mut self, setting_type: SettingType) -> u64 {
            if let Some(count) = self.request_counts.get(&setting_type) {
                *count
            } else {
                0
            }
        }
    }

    impl SettingHandlerFactory for FakeFactory {
        fn generate(&mut self, setting_type: SettingType) -> Option<SettingHandler> {
            let existing_count = self.get_request_count(setting_type);

            if let Some(handler) = self.handlers.get(&setting_type) {
                self.request_counts.insert(setting_type, existing_count + 1);
                return Some(handler.clone());
            } else {
                return None;
            }
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_notify() {
        let conduit_handle = ConduitImpl::create();
        let (conduit_sender, mut conduit_receiver) = conduit_handle.lock().await.create_waypoint();
        let handler_factory = Arc::new(Mutex::new(FakeFactory::new()));
        let _registry = RegistryImpl::create(handler_factory.clone(), conduit_handle.clone());
        let setting_type = SettingType::Unknown;

        let (handler_tx, mut handler_rx) = futures::channel::mpsc::unbounded::<Command>();
        handler_factory.lock().await.register(setting_type, handler_tx);

        // Send a listen state and make sure sink is notified.
        {
            conduit_sender.send(ConduitData::Action(SettingAction {
                id: 1,
                setting_type: setting_type,
                data: SettingActionData::Listen(1),
            }));

            let command = handler_rx.next().await.unwrap();

            match command {
                Command::ChangeState(State::Listen(notifier)) => {
                    // Send back notification and make sure it is received.
                    assert!(notifier.unbounded_send(setting_type).is_ok());
                    match conduit_receiver.next().await.unwrap() {
                        ConduitData::Event(SettingEvent::Changed(changed_type)) => {
                            assert_eq!(changed_type, setting_type);
                        }
                        _ => {
                            panic!("wrong response received");
                        }
                    }
                }
                _ => {
                    panic!("incorrect command received");
                }
            }
        }

        // Send an end listen state and make sure sink is notified.
        {
            conduit_sender.send(ConduitData::Action(SettingAction {
                id: 1,
                setting_type: setting_type,
                data: SettingActionData::Listen(0),
            }));

            match handler_rx.next().await.unwrap() {
                Command::ChangeState(State::EndListen) => {
                    // Success case - ignore.
                }
                _ => {
                    panic!("unexpected command");
                }
            }
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_request() {
        let handler_factory = Arc::new(Mutex::new(FakeFactory::new()));
        let conduit_handle = ConduitImpl::create();
        let (conduit_sender, mut conduit_receiver) = conduit_handle.lock().await.create_waypoint();
        let _registry = RegistryImpl::create(handler_factory.clone(), conduit_handle.clone());
        let setting_type = SettingType::Unknown;
        let request_id = 42;

        let (handler_tx, mut handler_rx) = futures::channel::mpsc::unbounded::<Command>();

        handler_factory.lock().await.register(setting_type, handler_tx);

        // Send initial request.
        conduit_sender.send(ConduitData::Action(SettingAction {
            id: request_id,
            setting_type: setting_type,
            data: SettingActionData::Request(SettingRequest::Get),
        }));

        let command = handler_rx.next().await.unwrap();

        match command {
            Command::HandleRequest(request, responder) => {
                assert_eq!(request, SettingRequest::Get);
                // Send back response
                assert!(responder.send(Ok(None)).is_ok());

                // verify response matches
                match conduit_receiver.next().await.unwrap() {
                    ConduitData::Event(SettingEvent::Response(response_id, response)) => {
                        assert_eq!(request_id, response_id);
                        assert!(response.is_ok());
                        assert_eq!(None, response.unwrap());
                    }
                    _ => {
                        panic!("unexpected response");
                    }
                }
            }
            _ => {
                panic!("incorrect command received");
            }
        }
    }

    /// Ensures setting handler is only generated once.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_generation() {
        let handler_factory = Arc::new(Mutex::new(FakeFactory::new()));
        let conduit_handle = ConduitImpl::create();
        let (conduit_sender, _conduit_receiver) = conduit_handle.lock().await.create_waypoint();
        let _registry = RegistryImpl::create(handler_factory.clone(), conduit_handle.clone());
        let setting_type = SettingType::Unknown;
        let request_id = 42;

        let (handler_tx, mut handler_rx) = futures::channel::mpsc::unbounded::<Command>();

        handler_factory.lock().await.register(setting_type, handler_tx);

        // Send initial request.
        conduit_sender.send(ConduitData::Action(SettingAction {
            id: request_id,
            setting_type: setting_type,
            data: SettingActionData::Request(SettingRequest::Get),
        }));

        // Capture request.
        let _ = handler_rx.next().await.unwrap();

        // Ensure the handler was only created once.
        assert_eq!(1, handler_factory.lock().await.get_request_count(setting_type));

        // Send followup request.
        conduit_sender.send(ConduitData::Action(SettingAction {
            id: request_id,
            setting_type: setting_type,
            data: SettingActionData::Request(SettingRequest::Get),
        }));

        // Capture request.
        let _ = handler_rx.next().await.unwrap();

        // Make sure no followup generation was invoked.
        assert_eq!(1, handler_factory.lock().await.get_request_count(setting_type));
    }
}
