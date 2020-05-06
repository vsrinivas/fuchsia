// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

///! Serves WLAN policy listener update APIs.
///!
use {
    fidl_fuchsia_wlan_policy as fidl_policy,
    futures::{channel::mpsc, future::BoxFuture, prelude::*, select, stream::FuturesUnordered},
};

pub trait Listener<U> {
    /// Sends an update to the listener.  Returns itself boxed if the update was sent successfully.
    fn notify_listener(self, update: U) -> BoxFuture<'static, Option<Box<Self>>>;
}

pub trait UpdateCloner {
    fn clone(&self) -> Self;
    fn default() -> Self;
}

/// Messages sent to update the current state and notify listeners or add new listeners.
#[derive(Debug)]
pub enum Message<P, U> {
    /// Sent if a new listener wants to register itself for future updates.
    NewListener(P),
    /// Sent if an entity wants to notify all listeners about a state change.
    #[allow(unused)]
    NotifyListeners(U),
}

/// Serves and manages a list of Listeners.
/// Use `Message` to interact with registered listeners.
pub async fn serve<P, U>(mut messages: mpsc::UnboundedReceiver<Message<P, U>>)
where
    P: Listener<U>,
    U: UpdateCloner,
{
    // A list of listeners which are ready to receive updates.
    let mut acked_listeners = Vec::new();
    // A list of pending listeners which have not acknowledged their last update yet.
    let mut unacked_listeners = FuturesUnordered::new();
    // Last reported state update.
    let mut current_state = U::default();

    loop {
        select! {
            // Enqueue every listener back into the listener pool once they ack'ed the reception
            // of a previously sent update.
            // Listeners which already closed their channel will be dropped.
            // TODO(38128): Clients must be sent the latest update if they weren't updated due to
            // their inactivity.
            listener = unacked_listeners.select_next_some() => if let Some(listener) = listener {
                acked_listeners.push(listener);
            },
            // Message for listeners
            msg = messages.select_next_some() => match msg {
                // Register new listener
                Message::NewListener(listener) => {
                    unacked_listeners.push(listener.notify_listener(current_state.clone()));
                },
                // Notify all listeners
                Message::NotifyListeners(update) => {
                    current_state = update.clone();
                    // Listeners are dequeued and their pending acknowledgement is enqueued.
                    while !acked_listeners.is_empty() {
                        let listener = acked_listeners.remove(0);
                        unacked_listeners.push(listener.notify_listener(update.clone()));
                    }
                },
            },
        }
    }
}

impl UpdateCloner for fidl_policy::ClientStateSummary {
    fn clone(&self) -> fidl_policy::ClientStateSummary {
        fidl_policy::ClientStateSummary {
            state: self.state.clone(),
            networks: self.networks.as_ref().map(|x| {
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

    fn default() -> fidl_policy::ClientStateSummary {
        fidl_policy::ClientStateSummary { state: None, networks: None }
    }
}

impl Listener<fidl_policy::ClientStateSummary> for fidl_policy::ClientStateUpdatesProxy {
    fn notify_listener(
        self,
        update: fidl_policy::ClientStateSummary,
    ) -> BoxFuture<'static, Option<Box<Self>>> {
        let fut =
            async move { self.on_client_state_update(update).await.ok().map(|()| Box::new(self)) };
        fut.boxed()
    }
}

// Helpful aliases for servicing client updates
pub type ClientMessage =
    Message<fidl_policy::ClientStateUpdatesProxy, fidl_policy::ClientStateSummary>;
pub type ClientMessageSender = mpsc::UnboundedSender<ClientMessage>;

impl UpdateCloner for fidl_policy::AccessPointState {
    fn clone(&self) -> fidl_policy::AccessPointState {
        fidl_policy::AccessPointState {
            state: self.state,
            mode: self.mode,
            band: self.band,
            frequency: self.frequency,
            clients: self
                .clients
                .as_ref()
                .map(|info| fidl_policy::ConnectedClientInformation { count: info.count }),
        }
    }

    fn default() -> fidl_policy::AccessPointState {
        fidl_policy::AccessPointState {
            state: None,
            mode: None,
            band: None,
            frequency: None,
            clients: None,
        }
    }
}

impl Listener<fidl_policy::AccessPointState> for fidl_policy::AccessPointStateUpdatesProxy {
    fn notify_listener(
        self,
        update: fidl_policy::AccessPointState,
    ) -> BoxFuture<'static, Option<Box<Self>>> {
        let fut = async move {
            // If there is no state information, send back an empty set of updates.
            let mut update = match update.state {
                Some(_) => vec![update],
                None => vec![],
            };
            let mut iter = update.drain(..);
            let fut = self.on_access_point_state_update(&mut iter);
            fut.await.ok().map(|()| Box::new(self))
        };
        fut.boxed()
    }
}

// Helpful aliases for servicing Ap updates
pub type ApMessage =
    Message<fidl_policy::AccessPointStateUpdatesProxy, fidl_policy::AccessPointState>;
pub type ApMessageSender = mpsc::UnboundedSender<ApMessage>;

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy, fuchsia_async as fasync, futures::task::Poll,
        pin_utils::pin_mut, wlan_common::assert_variant,
    };

    #[test]
    fn initial_update() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded::<ClientMessage>();
        let serve_listeners = serve::<
            fidl_policy::ClientStateUpdatesProxy,
            fidl_policy::ClientStateSummary,
        >(listener_updates);
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
        let (mut update_sender, listener_updates) = mpsc::unbounded::<ClientMessage>();
        let serve_listeners = serve::<
            fidl_policy::ClientStateUpdatesProxy,
            fidl_policy::ClientStateSummary,
        >(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register #1 listener & ack initial update.
        let mut l1_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // Register #2 listener & ack initial update.
        let mut l2_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);

        // Send an update to both listeners.
        let update = fidl_policy::ClientStateSummary { state: None, networks: Some(vec![]) };
        broadcast_update(&mut exec, &mut update_sender, update.clone(), &mut serve_listeners);

        // Verify #1 listener received the update.
        let summary = ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(summary, fidl_policy::ClientStateSummary::from(update.clone()));
        assert_variant!(exec.run_until_stalled(&mut l1_stream.next()), Poll::Pending);

        // Verify #2 listeners received the update.
        let summary = ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);
        assert_eq!(summary, fidl_policy::ClientStateSummary::from(update.clone()));
        assert_variant!(exec.run_until_stalled(&mut l2_stream.next()), Poll::Pending);
    }

    #[test]
    fn multiple_listeners_unacked() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (mut update_sender, listener_updates) = mpsc::unbounded::<ClientMessage>();
        let serve_listeners = serve::<
            fidl_policy::ClientStateUpdatesProxy,
            fidl_policy::ClientStateSummary,
        >(listener_updates);
        pin_mut!(serve_listeners);
        assert_variant!(exec.run_until_stalled(&mut serve_listeners), Poll::Pending);

        // Register #1 listener & ack initial update.
        let mut l1_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // Register #2 listener & ack initial update.
        let mut l2_stream = register_listener(&mut exec, &mut update_sender, &mut serve_listeners);
        ack_next_status_update(&mut exec, &mut l2_stream, &mut serve_listeners);

        // Send an update to both listeners.
        let update = fidl_policy::ClientStateSummary { state: None, networks: Some(vec![]) };
        broadcast_update(&mut exec, &mut update_sender, update.clone(), &mut serve_listeners);

        // #1 listener acknowledges update.
        ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);

        // #2 listener does not yet acknowledge update.
        let (_, l2_responder) =
            try_next_status_update(&mut exec, &mut l2_stream).expect("expected status update");

        // Send another update.
        let update = fidl_policy::ClientStateSummary { state: None, networks: None };
        broadcast_update(&mut exec, &mut update_sender, update.clone(), &mut serve_listeners);

        // #1 listener verifies and acknowledges update.
        let summary = ack_next_status_update(&mut exec, &mut l1_stream, &mut serve_listeners);
        assert_eq!(summary, update.clone());

        // #2 listener should not have been sent an update.
        assert_variant!(exec.run_until_stalled(&mut l2_stream.next()), Poll::Pending);

        // #2 listener will send ack previous update.
        ack_update(&mut exec, l2_responder, &mut serve_listeners);

        // Send another update.
        let update = fidl_policy::ClientStateSummary { state: None, networks: None };
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
        sender: &mut ClientMessageSender,
        update: fidl_policy::ClientStateSummary,
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
        sender: &mut ClientMessageSender,
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
