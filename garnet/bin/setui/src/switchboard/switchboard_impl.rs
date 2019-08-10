// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::switchboard::base::*;

use failure::Error;

use futures::channel::mpsc::UnboundedSender;

use std::collections::HashMap;
use std::sync::{Arc, RwLock};

use fuchsia_async as fasync;
use futures::stream::StreamExt;
use futures::TryFutureExt;

type ResponderMap = HashMap<u64, SettingRequestResponder>;
type Listener = UnboundedSender<SettingType>;
type ListenerMap = HashMap<SettingType, Vec<Listener>>;

pub struct SwitchboardImpl {
    /// Next available id.
    next_action_id: u64,
    /// Acquired during construction and used internally to send input.
    action_sender: UnboundedSender<SettingAction>,
    /// mapping of request output ids to responders.
    request_responders: ResponderMap,
    /// mapping of listeners for changes
    listeners: ListenerMap,
}

impl SwitchboardImpl {
    /// Creates a new SwitchboardImpl, which will return the instance along with
    /// a sender to provide events in response to the actions sent.
    pub fn create(
        action_sender: UnboundedSender<SettingAction>,
    ) -> (Arc<RwLock<SwitchboardImpl>>, UnboundedSender<SettingEvent>) {
        let (event_tx, mut event_rx) = futures::channel::mpsc::unbounded::<SettingEvent>();
        let switchboard = Arc::new(RwLock::new(Self {
            next_action_id: 0,
            action_sender: action_sender,
            request_responders: HashMap::new(),
            listeners: HashMap::new(),
        }));

        let switchboard_clone = switchboard.clone();
        fasync::spawn(
            async move {
                while let Some(event) = event_rx.next().await {
                    switchboard_clone.write().unwrap().process_event(event);
                }
                Ok(())
            }
                .unwrap_or_else(|_e: failure::Error| {}),
        );

        return (switchboard, event_tx);
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
            SettingEvent::Response(action_id, response) => {
                self.handle_response(action_id, response);
            }
        }
    }

    fn notify_listeners(&self, setting_type: SettingType) {
        if let Some(listeners) = self.listeners.get(&setting_type) {
            for listener in listeners {
                listener.unbounded_send(setting_type).ok();
            }
        }
    }

    fn handle_response(&mut self, origin_id: u64, response: SettingResponseResult) {
        if let Some(responder) = self.request_responders.remove(&origin_id) {
            responder.send(response).ok();
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
        // Associate request
        let action_id = self.get_next_action_id();
        self.request_responders.insert(action_id, callback);

        self.action_sender.unbounded_send(SettingAction {
            id: action_id,
            setting_type,
            data: SettingActionData::Request(request),
        })?;

        return Ok(());
    }

    fn listen(
        &mut self,
        setting_type: SettingType,
        listener: UnboundedSender<SettingType>,
    ) -> Result<(), Error> {
        let action_id = self.get_next_action_id();

        if !self.listeners.contains_key(&setting_type) {
            self.listeners.insert(setting_type, vec![]);
        }

        if let Some(listeners) = self.listeners.get_mut(&setting_type) {
            listeners.push(listener);

            self.action_sender.unbounded_send(SettingAction {
                id: action_id,
                setting_type,
                data: SettingActionData::Listen(listeners.len() as u64),
            })?;
        }

        return Ok(());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_request() {
        let (action_tx, mut action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();
        let (switchboard, event_tx) = SwitchboardImpl::create(action_tx);
        let (response_tx, response_rx) =
            futures::channel::oneshot::channel::<SettingResponseResult>();

        // Send request
        assert!(switchboard
            .write()
            .unwrap()
            .request(SettingType::Unknown, SettingRequest::Get, response_tx)
            .is_ok());

        // Ensure request is received.
        let action = action_rx.next().await.unwrap();

        assert_eq!(SettingType::Unknown, action.setting_type);
        if let SettingActionData::Request(request) = action.data {
            assert_eq!(request, SettingRequest::Get);
        } else {
            panic!("unexpected output type");
        }

        // Send response
        assert!(event_tx.unbounded_send(SettingEvent::Response(action.id, Ok(None))).is_ok());

        // Ensure response is received.
        let response = response_rx.await.unwrap();
        assert!(response.is_ok());
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_notify() {
        let (action_tx, mut action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();
        let (switchboard, event_tx) = SwitchboardImpl::create(action_tx);
        let setting_type = SettingType::Unknown;

        // Register first listener and verify count.
        let (notify_tx1, mut notify_rx1) = futures::channel::mpsc::unbounded::<SettingType>();
        assert!(switchboard.write().unwrap().listen(setting_type, notify_tx1).is_ok());
        {
            let action = action_rx.next().await.unwrap();

            assert_eq!(action.setting_type, setting_type);
            assert_eq!(action.data, SettingActionData::Listen(1));
        }

        // Register second listener and verify count
        let (notify_tx2, mut notify_rx2) = futures::channel::mpsc::unbounded::<SettingType>();
        assert!(switchboard.write().unwrap().listen(setting_type, notify_tx2).is_ok());
        {
            let action = action_rx.next().await.unwrap();

            assert_eq!(action.setting_type, setting_type);
            assert_eq!(action.data, SettingActionData::Listen(2));
        }

        // Send notification
        assert!(event_tx.unbounded_send(SettingEvent::Changed(setting_type)).is_ok());

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
