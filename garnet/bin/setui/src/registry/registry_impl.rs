// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::base::{Command, Registry, State};
use crate::switchboard::base::{
    SettingAction, SettingActionData, SettingEvent, SettingRequest, SettingResponseResult,
    SettingType,
};

use anyhow::{format_err, Context as _, Error};
use fuchsia_async as fasync;

use futures::channel::mpsc::UnboundedReceiver;
use futures::channel::mpsc::UnboundedSender;
use futures::stream::StreamExt;
use futures::TryFutureExt;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

pub struct RegistryImpl {
    /// A mapping of setting types to senders, used to relay new commands.
    command_sender_map: HashMap<SettingType, UnboundedSender<Command>>,
    /// Used to send messages back to the source producing messages on the
    /// receiver provided at construction.
    event_sender: UnboundedSender<SettingEvent>,
    /// A sender handed as part of State::Listen to allow entities to provide
    /// back updates.
    /// TODO(SU-334): Investigate restricting the setting type a sender may
    /// specify to the one registered.
    notification_sender: UnboundedSender<SettingType>,
    /// The current types being listened in on.
    active_listeners: Vec<SettingType>,
}

impl RegistryImpl {
    /// Returns a handle to a RegistryImpl which is listening to SettingAction from the
    /// provided receiver and will send responses/updates on the given sender.
    pub fn create(
        sender: UnboundedSender<SettingEvent>,
        mut receiver: UnboundedReceiver<SettingAction>,
    ) -> Arc<RwLock<RegistryImpl>> {
        let (notification_tx, mut notification_rx) =
            futures::channel::mpsc::unbounded::<SettingType>();

        // We must create handle here rather than return back the value as we
        // reference the registry in the async tasks below.
        let registry = Arc::new(RwLock::new(Self {
            command_sender_map: HashMap::new(),
            event_sender: sender,
            notification_sender: notification_tx,
            active_listeners: vec![],
        }));

        // An async task is spawned here to listen to the SettingAction for new
        // directives. Note that the receiver is the one provided in the arguments.
        {
            let registry_clone = registry.clone();
            fasync::spawn(
                async move {
                    while let Some(action) = receiver.next().await {
                        registry_clone.write().process_action(action);
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
                        registry_clone.write().notify(setting_type);
                    }
                    Ok(())
                }
                .unwrap_or_else(|_e: anyhow::Error| {}),
            );
        }

        return registry;
    }

    /// Interpret action from switchboard into registry actions.
    fn process_action(&mut self, action: SettingAction) {
        match action.data {
            SettingActionData::Request(request) => {
                self.process_request(action.id, action.setting_type, request);
            }
            SettingActionData::Listen(size) => {
                self.process_listen(action.setting_type, size);
            }
        }
    }

    /// Notifies proper sink in the case the notification listener count is
    /// non-zero and we aren't already listening for changes to the type or there
    /// are no more listeners and we are actively listening.
    fn process_listen(&mut self, setting_type: SettingType, size: u64) {
        let candidate = self.command_sender_map.get(&setting_type);

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
            self.event_sender.unbounded_send(SettingEvent::Changed(setting_type)).ok();
        }
    }

    /// Forwards request to proper sink. A new task is spawned in order to receive
    /// the response. If no sink is available, an error is immediately reported
    /// back.
    fn process_request(&self, id: u64, setting_type: SettingType, request: SettingRequest) {
        let candidate = self.command_sender_map.get(&setting_type);
        match candidate {
            None => {
                self.event_sender
                    .unbounded_send(SettingEvent::Response(
                        id,
                        Err(format_err!("no handler for requested type")),
                    ))
                    .ok();
            }
            Some(command_sender) => {
                let (responder, receiver) =
                    futures::channel::oneshot::channel::<SettingResponseResult>();
                let sender_clone = self.event_sender.clone();
                let error_sender_clone = self.event_sender.clone();
                fasync::spawn(
                    async move {
                        let response =
                            receiver.await.context("getting response from controller")?;
                        sender_clone.unbounded_send(SettingEvent::Response(id, response)).ok();

                        Ok(())
                    }
                    .unwrap_or_else(move |e: anyhow::Error| {
                        error_sender_clone.unbounded_send(SettingEvent::Response(id, Err(e))).ok();
                    }),
                );

                command_sender.unbounded_send(Command::HandleRequest(request, responder)).ok();
            }
        }
    }
}

impl Registry for RegistryImpl {
    /// Associates the provided command sender with the given type to receive
    /// future commands and updates. If a sender has already been associated with
    /// the type, returns an error.
    fn register(
        &mut self,
        setting_type: SettingType,
        command_sender: UnboundedSender<Command>,
    ) -> Result<(), Error> {
        if self.command_sender_map.contains_key(&setting_type) {
            return Err(format_err!("SettingType is already registered for type"));
        }
        self.command_sender_map.insert(setting_type, command_sender);
        return Ok(());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_notify() {
        let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();
        let (event_tx, mut event_rx) = futures::channel::mpsc::unbounded::<SettingEvent>();
        let registry = RegistryImpl::create(event_tx, action_rx);
        let setting_type = SettingType::Unknown;

        let (handler_tx, mut handler_rx) = futures::channel::mpsc::unbounded::<Command>();

        assert!(registry.write().register(setting_type, handler_tx).is_ok());

        // Send a listen state and make sure sink is notified.
        {
            assert!(action_tx
                .unbounded_send(SettingAction {
                    id: 1,
                    setting_type: setting_type,
                    data: SettingActionData::Listen(1)
                })
                .is_ok());

            let command = handler_rx.next().await.unwrap();

            match command {
                Command::ChangeState(State::Listen(notifier)) => {
                    // Send back notification and make sure it is received.
                    assert!(notifier.unbounded_send(setting_type).is_ok());
                    match event_rx.next().await.unwrap() {
                        SettingEvent::Changed(changed_type) => {
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
            assert!(action_tx
                .unbounded_send(SettingAction {
                    id: 1,
                    setting_type: setting_type,
                    data: SettingActionData::Listen(0)
                })
                .is_ok());

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
        let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();
        let (event_tx, mut event_rx) = futures::channel::mpsc::unbounded::<SettingEvent>();
        let registry = RegistryImpl::create(event_tx, action_rx);
        let setting_type = SettingType::Unknown;
        let request_id = 42;

        let (handler_tx, mut handler_rx) = futures::channel::mpsc::unbounded::<Command>();

        assert!(registry.write().register(setting_type, handler_tx).is_ok());

        // Send initial request.
        assert!(action_tx
            .unbounded_send(SettingAction {
                id: request_id,
                setting_type: setting_type,
                data: SettingActionData::Request(SettingRequest::Get)
            })
            .is_ok());

        let command = handler_rx.next().await.unwrap();

        match command {
            Command::HandleRequest(request, responder) => {
                assert_eq!(request, SettingRequest::Get);
                // Send back response
                assert!(responder.send(Ok(None)).is_ok());

                // verify response matches
                match event_rx.next().await.unwrap() {
                    SettingEvent::Response(response_id, response) => {
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
}
