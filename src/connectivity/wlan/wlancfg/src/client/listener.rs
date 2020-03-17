// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Serves ClientStateUpdate listeners.
///!
use {
    fidl_fuchsia_wlan_policy as fidl_policy,
    futures::{channel::mpsc, prelude::*, select, stream::FuturesUnordered},
    parking_lot::Mutex,
    std::sync::Arc,
};

/// Convenience wrapper for cloning a `ClientStateSummary`.
struct ClientStateSummaryCloner(fidl_policy::ClientStateSummary);

impl ClientStateSummaryCloner {
    fn clone(&self) -> fidl_policy::ClientStateSummary {
        fidl_policy::ClientStateSummary {
            state: self.0.state.clone(),
            networks: self.0.networks.as_ref().map(|x| {
                x.iter()
                    .map(|x| fidl_policy::NetworkState {
                        id: x.id.clone(),
                        state: x.state.clone(),
                        status: x.status.clone(),
                    })
                    .collect()
            }),
        }
    }
}

impl From<fidl_policy::ClientStateSummary> for ClientStateSummaryCloner {
    fn from(summary: fidl_policy::ClientStateSummary) -> Self {
        Self(summary)
    }
}

/// Messages sent by either of the two served service to interact with a shared pool of listeners.
#[derive(Debug)]
pub enum Message {
    /// Sent if a new listener wants to register itself for future updates.
    NewListener(fidl_policy::ClientStateUpdatesProxy),
    /// Sent if an entity wants to notify all listeners about a state change.
    #[allow(unused)]
    NotifyListeners(fidl_policy::ClientStateSummary),
}
pub type MessageSender = mpsc::UnboundedSender<Message>;
pub type MessageStream = mpsc::UnboundedReceiver<Message>;

/// Serves and manages a list of ClientListener.
/// Use `Message` to interact with registered listeners.
pub async fn serve(mut messages: MessageStream) {
    // A list of listeners which are ready to receive updates.
    let acked_listeners = Arc::new(Mutex::new(vec![]));
    // A list of pending listeners which have not acknowledged their last update yet.
    let mut unacked_listeners = FuturesUnordered::new();
    loop {
        select! {
            // Enqueue every listener back into the listener pool once they ack'ed the reception
            // of a previously sent update.
            // Listeners which already closed their channel will be dropped.
            // TODO(38128): Clients must be send the latest update if they weren't updated due to
            // their inactivity.
            listener = unacked_listeners.select_next_some() => if let Some(listener) = listener {
                acked_listeners.lock().push(listener);
            },
            // Message for listeners
            msg = messages.select_next_some() => match msg {
                // Register new listener
                Message::NewListener(listener) => {
                    unacked_listeners.push(notify_listener(listener, current_state()));
                },
                // Notify all listeners
                Message::NotifyListeners(update) => {
                    let update = ClientStateSummaryCloner(update);
                    let mut listeners = acked_listeners.lock();
                    // Listeners are dequeued and their pending acknowledgement is enqueued.
                    while !listeners.is_empty() {
                        let listener = listeners.remove(0);
                        unacked_listeners.push(notify_listener(listener, update.clone()));
                    }
                },
            },
        }
    }
}

/// Notifies a listener about the given update.
/// Returns Some(listener) if the update was successful, otherwise None.
async fn notify_listener(
    listener: fidl_policy::ClientStateUpdatesProxy,
    update: fidl_policy::ClientStateSummary,
) -> Option<fidl_policy::ClientStateUpdatesProxy> {
    listener.on_client_state_update(update).await.ok().map(|()| listener)
}

/// Returns the current state of the active Client.
/// Right now, only a dummy state update is returned.
fn current_state() -> fidl_policy::ClientStateSummary {
    // TODO(hahnr): Don't just send a dummy state update but the correct current state of the
    // interface.
    fidl_policy::ClientStateSummary { state: None, networks: None }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy, fuchsia_async as fasync, futures::task::Poll,
        pin_utils::pin_mut, wlan_common::assert_variant,
    };

    #[test]
    fn initial_update() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded();
        let serve_listeners = serve(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register listener.
        let mut l1_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);

        // Verify first listener received an update.
        let summary = ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(summary, fidl_policy::ClientStateSummary { state: None, networks: None });

        // Verify exactly one update was sent.
        assert_variant!(exec.run_until_stalled(&mut l1_stream.next()), Poll::Pending);
    }

    #[test]
    fn multiple_listeners_broadcast() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded();
        let serve_listeners = serve(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register #1 listener & ack initial update.
        let mut l1_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // Register #2 listener & ack initial update.
        let mut l2_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);

        // Send an update to both listeners.
        let update = ClientStateSummaryCloner(fidl_policy::ClientStateSummary {
            state: None,
            networks: Some(vec![]),
        });
        broadcast_update(&mut exec, &mut update_sender, update.clone(), &mut serve_listeners);

        // Verify #1 listener received the update.
        let summary = ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(summary, update.clone());
        assert_variant!(exec.run_until_stalled(&mut l1_stream.next()), Poll::Pending);

        // Verify #2 listeners received the update.
        let summary = ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);
        assert_eq!(summary, update.clone());
        assert_variant!(exec.run_until_stalled(&mut l2_stream.next()), Poll::Pending);
    }

    #[test]
    fn multiple_listeners_unacked() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded();
        let serve_listeners = serve(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register #1 listener & ack initial update.
        let mut l1_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // Register #2 listener & ack initial update.
        let mut l2_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);

        // Send an update to both listeners.
        let update = ClientStateSummaryCloner(fidl_policy::ClientStateSummary {
            state: None,
            networks: Some(vec![]),
        });
        broadcast_update(&mut exec, &mut update_sender, update.clone(), &mut serve_listeners);

        // #1 listener acknowledges update.
        ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // #2 listener does not yet acknowledge update.
        let (_, l2_responder) =
            try_next_status_update(&mut exec, &mut l2_stream).expect("expected status update");

        // Send another update.
        let update = ClientStateSummaryCloner(fidl_policy::ClientStateSummary {
            state: None,
            networks: None,
        });
        broadcast_update(&mut exec, &mut update_sender, update.clone(), &mut serve_listeners);

        // #1 listener verifies and acknowledges update.
        let summary = ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(summary, update.clone());

        // #2 listener should not have been sent an update.
        assert_variant!(exec.run_until_stalled(&mut l2_stream.next()), Poll::Pending);

        // #2 listener will send ack previous update.
        ack_update(&mut exec, l2_responder, &mut serve_listeners);

        // Send another update.
        let update = ClientStateSummaryCloner(fidl_policy::ClientStateSummary {
            state: None,
            networks: None,
        });
        broadcast_update(&mut exec, &mut update_sender, update.clone(), &mut serve_listeners);

        // Verify #1 & #2 listeners received the update.
        ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);
    }

    /// Acknowledges a previously received update.
    fn ack_update<F>(
        exec: &mut fasync::Executor,
        pending_ack: fidl_policy::ClientStateUpdatesOnClientStateUpdateResponder,
        serve_listeners: &mut F,
    ) where
        F: Future<Output = ()> + Unpin,
    {
        pending_ack.send().expect("error acking update");
        assert_variant!(exec.run_until_stalled(serve_listeners), Poll::Pending);
    }

    /// Broadcasts an update to all registered listeners.
    fn broadcast_update<F>(
        exec: &mut fasync::Executor,
        sender: &mut MessageSender,
        update: fidl_policy::ClientStateSummary,
        serve_listeners: &mut F,
    ) where
        F: Future<Output = ()> + Unpin,
    {
        let clone = ClientStateSummaryCloner(update).clone();
        sender.unbounded_send(Message::NotifyListeners(clone)).expect("error sending update");
        assert_variant!(exec.run_until_stalled(serve_listeners), Poll::Pending);
    }

    /// Reads and expects a status update to be available. Once the update was read it'll also be
    /// acknowledged.
    fn ack_next_status_update<F>(
        exec: &mut fasync::Executor,
        stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
        serve_listeners: &mut F,
    ) -> fidl_policy::ClientStateSummary
    where
        F: Future<Output = ()> + Unpin,
    {
        let (summary, responder) =
            try_next_status_update(exec, stream).expect("expected status update");
        ack_update(exec, responder, serve_listeners);
        summary
    }

    /// Registers a new listener.
    fn register_listener<F>(
        exec: &mut fasync::Executor,
        sender: &mut MessageSender,
        serve_listeners: &mut F,
    ) -> fidl_policy::ClientStateUpdatesRequestStream
    where
        F: Future<Output = ()> + Unpin,
    {
        // Register #1 listener.
        let (proxy, events) = create_proxy::<fidl_policy::ClientStateUpdatesMarker>()
            .expect("failed to create ClientStateUpdates proxy");
        let stream = events.into_stream().expect("failed to create stream");
        sender.unbounded_send(Message::NewListener(proxy)).expect("error sending update");
        assert_variant!(exec.run_until_stalled(serve_listeners), Poll::Pending);
        stream
    }

    /// Tries to read a status update. Returns None if no update was received.
    fn try_next_status_update(
        exec: &mut fasync::Executor,
        stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
    ) -> Option<(
        fidl_policy::ClientStateSummary,
        fidl_policy::ClientStateUpdatesOnClientStateUpdateResponder,
    )> {
        let next = exec.run_until_stalled(&mut stream.next());
        if let Poll::Ready(Some(Ok(
            fidl_policy::ClientStateUpdatesRequest::OnClientStateUpdate { summary, responder },
        ))) = next
        {
            Some((summary, responder))
        } else {
            None
        }
    }
}
