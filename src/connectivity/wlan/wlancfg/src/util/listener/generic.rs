// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::future_with_metadata::FutureWithMetadata,
    futures::{channel::mpsc, future::BoxFuture, prelude::*, select, stream::FuturesUnordered},
};

pub trait Listener<F> {
    /// Sends an update to the listener.  Returns itself boxed if the update was sent successfully.
    fn notify_listener(self, update: F) -> BoxFuture<'static, Option<Box<Self>>>;
}

pub trait CurrentStateCache {
    fn merge_in_update(&mut self, update: Self);
    /// Removes states that should not be cached.
    fn purge(&mut self);
    fn default() -> Self;
}

/// Messages sent to update the current state and notify listeners or add new listeners.
#[derive(Debug)]
pub enum Message<P, U> {
    /// Sent if a new listener wants to register itself for future updates.
    NewListener(P),
    /// Sent if an entity wants to notify all listeners about a state change.
    NotifyListeners(U),
}

#[derive(Debug, Clone)]
struct PendingAckMetadata {
    missed_a_message: bool,
}

/// Serves and manages a list of Listeners.
/// Use `Message` to interact with registered listeners.
pub async fn serve<P, F, U>(mut messages: mpsc::UnboundedReceiver<Message<P, U>>)
where
    P: Listener<F>,
    U: CurrentStateCache + Clone + Into<F> + std::cmp::PartialEq,
{
    // A list of listeners which are ready to receive updates.
    let mut acked_listeners = Vec::new();
    // A list of pending listeners which have not acknowledged their last update yet.
    // Rust is failing to infer the return value for pending_acks() in the select! statement
    // below, so this ugly and verbose type annotation is required.
    let mut pending_acks: FuturesUnordered<FutureWithMetadata<Option<Box<P>>, PendingAckMetadata>> =
        FuturesUnordered::new();
    // Last reported state update.
    let mut current_state = U::default();

    // A helper function to dedupe logic of notifying listener and build a FutureWithMetadata
    let send_current_state = |listener: Box<P>, current_state: &U| {
        let pending_ack_fut = listener.notify_listener(current_state.clone().into());
        FutureWithMetadata::new(PendingAckMetadata { missed_a_message: false }, pending_ack_fut)
    };

    loop {
        select! {
            // Process listener acks
            (listener, metadata) = pending_acks.select_next_some() => {
                // Listeners which closed their channel will be None. Drop them silently.
                if let Some(listener) = listener {
                    // Check if the listener missed any messages while we were waiting for its ack
                    if metadata.missed_a_message {
                        // If yes, send the listener a snapshot of the current state
                        pending_acks.push(send_current_state(listener, &current_state));
                    } else {
                        // Enqueue the listener back into the listener pool
                        acked_listeners.push(listener);
                    };
                };
            },
            // Process internal messages
            msg = messages.select_next_some() => match msg {
                // Register new listener
                Message::NewListener(listener) => {
                    // Send the new listener a copy of the current state
                    pending_acks.push(send_current_state(Box::new(listener), &current_state));
                },
                // Notify all listeners
                Message::NotifyListeners(update) => {
                    let prev_state = current_state.clone();
                    current_state.merge_in_update(update);
                    if current_state != prev_state {
                        // Mark all listeners that are pending an ack as having missed an update
                        for pending_ack_fut in pending_acks.iter_mut() {
                            pending_ack_fut.metadata.missed_a_message = true;
                        };
                        // Send an update to all listeners who are ready
                        while !acked_listeners.is_empty() {
                            // Listeners are dequeued and their pending acknowledgement is enqueued.
                            let listener = acked_listeners.remove(0);
                            pending_acks.push(send_current_state(listener, &current_state));
                        }
                        // Purge any parts of the current state that should not be sent again
                        current_state.purge();
                    }
                },
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::super::*,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_async as fasync,
        futures::{channel::mpsc, prelude::*, task::Poll},
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    // A collection of test utilities to test the Generic functions using Client impl
    mod test_utils {
        use super::*;

        /// Acknowledges a previously received update.
        pub fn ack_update<F>(
            exec: &mut fasync::TestExecutor,
            pending_ack: fidl_policy::ClientStateUpdatesOnClientStateUpdateResponder,
            serve_listeners: &mut F,
        ) where
            F: Future<Output = ()> + Unpin,
        {
            pending_ack.send().expect("error acking update");
            assert_variant!(exec.run_until_stalled(serve_listeners), Poll::Pending);
        }

        /// Broadcasts an update to all registered listeners.
        pub fn broadcast_update<F>(
            exec: &mut fasync::TestExecutor,
            sender: &mut ClientListenerMessageSender,
            update: ClientStateUpdate,
            serve_listeners: &mut F,
        ) where
            F: Future<Output = ()> + Unpin,
        {
            let clone = update.clone();
            sender.unbounded_send(Message::NotifyListeners(clone)).expect("error sending update");
            assert_variant!(exec.run_until_stalled(serve_listeners), Poll::Pending);
        }

        /// Reads and expects a status update to be available. Once the update was read it'll also be
        /// acknowledged.
        pub fn ack_next_status_update<F>(
            exec: &mut fasync::TestExecutor,
            stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
            serve_listeners: &mut F,
        ) -> fidl_policy::ClientStateSummary
        where
            F: Future<Output = ()> + Unpin,
        {
            let (summary, responder) =
                test_utils::try_next_status_update(exec, stream).expect("expected status update");
            ack_update(exec, responder, serve_listeners);
            summary
        }

        /// Registers a new listener.
        pub fn register_listener<F>(
            exec: &mut fasync::TestExecutor,
            sender: &mut ClientListenerMessageSender,
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
        pub fn try_next_status_update(
            exec: &mut fasync::TestExecutor,
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

    #[fuchsia::test]
    fn initial_update() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded::<ClientListenerMessage>();
        let serve_listeners = serve::<
            fidl_policy::ClientStateUpdatesProxy,
            fidl_policy::ClientStateSummary,
            ClientStateUpdate,
        >(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register listener.
        let mut l1_stream =
            test_utils::register_listener(&mut exec, &mut update_sender, &mut serve_listeners);

        // Verify first listener received an update.
        let summary =
            test_utils::ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(
            summary,
            fidl_policy::ClientStateSummary {
                state: Some(fidl_policy::WlanClientState::ConnectionsDisabled),
                networks: Some(vec![]),
                ..fidl_policy::ClientStateSummary::EMPTY
            }
        );

        // Verify exactly one update was sent.
        assert_variant!(exec.run_until_stalled(&mut l1_stream.next()), Poll::Pending);
    }

    #[fuchsia::test]
    fn multiple_listeners_broadcast() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded::<ClientListenerMessage>();
        let serve_listeners = serve::<
            fidl_policy::ClientStateUpdatesProxy,
            fidl_policy::ClientStateSummary,
            ClientStateUpdate,
        >(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register #1 listener & ack initial update.
        let mut l1_stream =
            test_utils::register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        let _ = test_utils::ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // Register #2 listener & ack initial update.
        let mut l2_stream =
            test_utils::register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        let _ = test_utils::ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);

        // Send an update to both listeners.
        let update = ClientStateUpdate {
            state: fidl_policy::WlanClientState::ConnectionsEnabled,
            networks: vec![],
        };
        let expected_summary = fidl_policy::ClientStateSummary {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: Some(vec![]),
            ..fidl_policy::ClientStateSummary::EMPTY
        };
        test_utils::broadcast_update(
            &mut exec,
            &mut update_sender,
            update.clone(),
            &mut serve_listeners,
        );

        // Verify #1 listener received the update.
        let summary =
            test_utils::ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(summary, expected_summary);
        assert_variant!(exec.run_until_stalled(&mut l1_stream.next()), Poll::Pending);

        // Verify #2 listeners received the update.
        let summary =
            test_utils::ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);
        assert_eq!(summary, expected_summary);
        assert_variant!(exec.run_until_stalled(&mut l2_stream.next()), Poll::Pending);
    }

    #[fuchsia::test]
    fn multiple_listeners_unacked() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded::<ClientListenerMessage>();
        let serve_listeners = serve::<
            fidl_policy::ClientStateUpdatesProxy,
            fidl_policy::ClientStateSummary,
            ClientStateUpdate,
        >(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register #1 listener & ack initial update.
        let mut l1_stream =
            test_utils::register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        let _ = test_utils::ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // Register #2 listener & ack initial update.
        let mut l2_stream =
            test_utils::register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        let _ = test_utils::ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);

        // Send an update to both listeners.
        let update = ClientStateUpdate {
            state: fidl_policy::WlanClientState::ConnectionsEnabled,
            networks: vec![],
        };
        test_utils::broadcast_update(
            &mut exec,
            &mut update_sender,
            update.clone(),
            &mut serve_listeners,
        );

        // #1 listener acknowledges update.
        let _ = test_utils::ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        // #2 listener does not yet acknowledge update.
        let (_, l2_responder) = test_utils::try_next_status_update(&mut exec, &mut l2_stream)
            .expect("expected status update");
        // Send another update.
        let update = ClientStateUpdate {
            state: fidl_policy::WlanClientState::ConnectionsDisabled,
            networks: vec![],
        };
        test_utils::broadcast_update(
            &mut exec,
            &mut update_sender,
            update.clone(),
            &mut serve_listeners,
        );

        // #1 listener verifies and acknowledges update.
        let summary =
            test_utils::ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        let expected_summary = fidl_policy::ClientStateSummary {
            state: Some(fidl_policy::WlanClientState::ConnectionsDisabled),
            networks: Some(vec![]),
            ..fidl_policy::ClientStateSummary::EMPTY
        };
        assert_eq!(summary, expected_summary);

        // #2 listener should not have been sent an update.
        assert_variant!(exec.run_until_stalled(&mut l2_stream.next()), Poll::Pending);

        // #2 listener will send ack for previous update.
        test_utils::ack_update(&mut exec, l2_responder, &mut serve_listeners);

        // #2 listener should have been sent a current state summary, since they missed an update.
        let summary =
            test_utils::ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);
        assert_eq!(summary, expected_summary);

        // Send another update.
        let update = ClientStateUpdate {
            state: fidl_policy::WlanClientState::ConnectionsEnabled,
            networks: vec![],
        };
        test_utils::broadcast_update(
            &mut exec,
            &mut update_sender,
            update.clone(),
            &mut serve_listeners,
        );

        // Verify #1 & #2 listeners received the update.
        let expected_summary = fidl_policy::ClientStateSummary {
            state: Some(fidl_policy::WlanClientState::ConnectionsEnabled),
            networks: Some(vec![]),
            ..fidl_policy::ClientStateSummary::EMPTY
        };
        let summary =
            test_utils::ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(summary, expected_summary);
        let summary =
            test_utils::ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);
        assert_eq!(summary, expected_summary);

        // No further updates
        assert_variant!(exec.run_until_stalled(&mut l1_stream.next()), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut l2_stream.next()), Poll::Pending);
    }
}
