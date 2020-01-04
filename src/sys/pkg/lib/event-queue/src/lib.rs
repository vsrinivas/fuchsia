// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! This is a crate for broadcasting events to multiple clients and waiting for each client to
//! receive it before sending another event to that client. If an event was failed to send, or if
//! the number of events in the queue exceeds the limit, then the client is removed from the event
//! queue.
//!
//! # Example
//!
//! ```
//! #[derive(Clone)]
//! struct FooEvent {
//!     state: String,
//!     progress: u8,
//! }
//!
//! impl Event for FooEvent {
//!     fn can_merge(&self, other: &FooEvent) -> bool {
//!         self.state == other.state
//!     }
//! }
//!
//! struct FooNotifier {
//!     proxy: FooProxy,
//! }
//!
//! impl Notify<FooEvent> for FooNotifier {
//!     fn notify(&self, event: FooEvent) -> BoxFuture<'static, Result<(), ClosedClient>> {
//!         self.proxy.on_event(&event).map(|result| result.map_err(|_| ClosedClient)).boxed()
//!     }
//! }
//!
//! async fn foo(proxy: FooProxy) {
//!     let (event_queue, mut handle) = EventQueue::<FooNotifier, FooEvent>::new();
//!     let fut = async move {
//!         handle.add_client(FooNotifier { proxy }).await.unwrap();
//!         handle.queue_event(FooEvent { state: "new state".to_string(), progress: 0 }).await.unwrap();
//!     };
//!     future::join(fut, event_queue).await;
//! }
//! ```

use futures::{
    channel::mpsc,
    future::{select_all, BoxFuture},
    prelude::*,
    select,
};
use std::collections::VecDeque;

const DEFAULT_EVENTS_LIMIT: usize = 10;

/// The event type need to implement this trait to tell the event queue whether two consecutive
/// pending events can be merged into a single event, if `can_merge` returns true, the event queue
/// will replace the last event in the queue with the latest event.
pub trait Event: Clone {
    /// Returns whether this event can be merged with another event.
    fn can_merge(&self, other: &Self) -> bool;
}

/// The client is closed and should be removed from the event queue.
#[derive(Debug)]
pub struct ClosedClient;

/// The event queue future was dropped before calling control handle functions.
#[derive(Debug)]
pub struct EventQueueDropped;

/// This trait defines how an event should be notified for a client. The struct that implements
/// this trait can hold client specific data.
pub trait Notify<E>
where
    E: Event,
{
    /// If the event was notified successfully, the future should return `Ok(())`, otherwise if the
    /// client is closed, the future should return `Err(ClosedClient)` which will results in the
    /// corresponding client being removed from the event queue.
    fn notify(&self, event: E) -> BoxFuture<'static, Result<(), ClosedClient>>;
}

enum Command<N, E> {
    AddClient(N),
    Clear,
    QueueEvent(E),
}

/// A control handle that can control the event queue.
pub struct ControlHandle<N, E>
where
    N: Notify<E>,
    E: Event,
{
    sender: mpsc::Sender<Command<N, E>>,
}

impl<N, E> ControlHandle<N, E>
where
    N: Notify<E>,
    E: Event,
{
    fn new(sender: mpsc::Sender<Command<N, E>>) -> Self {
        ControlHandle { sender }
    }

    /// Add a new client to the event queue.
    /// If there were events queued before, the new client will receive the last event.
    pub async fn add_client(&mut self, notifier: N) -> Result<(), EventQueueDropped> {
        self.sender.send(Command::AddClient(notifier)).await.map_err(|_| EventQueueDropped)
    }

    /// Clear all clients and events from the event queue.
    pub async fn clear(&mut self) -> Result<(), EventQueueDropped> {
        self.sender.send(Command::Clear).await.map_err(|_| EventQueueDropped)
    }

    /// Queue an event that will be sent to all clients.
    pub async fn queue_event(&mut self, event: E) -> Result<(), EventQueueDropped> {
        self.sender.send(Command::QueueEvent(event)).await.map_err(|_| EventQueueDropped)
    }
}

/// An event queue for broadcasting events to multiple clients.
/// Clients that failed to receive events or do not receive events fast enough will be dropped.
pub struct EventQueue<N, E>
where
    N: Notify<E>,
    E: Event,
{
    clients: Vec<Client<N, E>>,
    receiver: mpsc::Receiver<Command<N, E>>,
    events_limit: usize,
    last_event: Option<E>,
}

impl<N, E> EventQueue<N, E>
where
    N: Notify<E>,
    E: Event,
{
    /// Create a new `EventQueue` and returns a future for the event queue and a control handle
    /// to control the event queue.
    pub fn new() -> (impl Future<Output = ()>, ControlHandle<N, E>) {
        Self::with_limit(DEFAULT_EVENTS_LIMIT)
    }

    /// Set the maximum number of events each client can have in the queue.
    /// Clients exceeding this limit will be dropped.
    /// The default value if not set is 10.
    pub fn with_limit(limit: usize) -> (impl Future<Output = ()>, ControlHandle<N, E>) {
        let (sender, receiver) = mpsc::channel(1);
        let event_queue =
            EventQueue { clients: Vec::new(), receiver, events_limit: limit, last_event: None };
        (event_queue.start(), ControlHandle::new(sender))
    }

    /// Start the event queue, this function will finish if the sender was dropped.
    async fn start(mut self) {
        loop {
            // select_all will panic if the iterator has nothing in it, so we chain a
            // future::pending to it.
            let mut pending = future::pending().boxed();
            let all_events = self
                .clients
                .iter_mut()
                .filter_map(|c| c.pending_event.as_mut())
                .chain(std::iter::once(&mut pending));
            let mut select_all_events = select_all(all_events).fuse();
            select! {
                (result, index, _) = select_all_events => {
                    let i = self.find_client_index(index);
                    match result {
                        Ok(()) => self.clients[i].next_event(),
                        Err(ClosedClient)=> {
                            self.clients.swap_remove(i);
                        },
                    }
                },
                command = self.receiver.next() => {
                    match command {
                        Some(Command::AddClient(proxy)) => self.add_client(proxy),
                        Some(Command::Clear) => self.clear(),
                        Some(Command::QueueEvent(event)) => self.queue_event(event),
                        None => break,
                    }
                },
            }
        }
    }

    fn add_client(&mut self, notifier: N) {
        let mut client = Client::new(notifier);
        if let Some(event) = &self.last_event {
            client.queue_event(event.clone(), self.events_limit);
        }
        self.clients.push(client);
    }

    fn clear(&mut self) {
        self.clients.clear();
        self.last_event = None;
    }

    fn queue_event(&mut self, event: E) {
        let mut i = 0;
        while i < self.clients.len() {
            if !self.clients[i].queue_event(event.clone(), self.events_limit) {
                self.clients.swap_remove(i);
            } else {
                i += 1;
            }
        }
        self.last_event = Some(event);
    }

    // Figure out the actual client index based on the filtered index.
    fn find_client_index(&self, index: usize) -> usize {
        let mut j = 0;
        for i in 0..self.clients.len() {
            if self.clients[i].pending_event.is_none() {
                continue;
            }

            if j == index {
                return i;
            }

            j += 1;
        }
        panic!("index {} too large", index);
    }
}

struct Client<N, E>
where
    N: Notify<E>,
    E: Event,
{
    notifier: N,
    pending_event: Option<BoxFuture<'static, Result<(), ClosedClient>>>,
    events: VecDeque<E>,
}

impl<N, E> Client<N, E>
where
    N: Notify<E>,
    E: Event,
{
    fn new(notifier: N) -> Self {
        Client { notifier, pending_event: None, events: VecDeque::new() }
    }

    fn queue_event(&mut self, event: E, events_limit: usize) -> bool {
        let mut num_events = self.events.len();
        if self.pending_event.is_some() {
            num_events += 1;
        }
        let can_merge =
            self.events.back().map(|last_event| last_event.can_merge(&event)).unwrap_or(false);
        let num_new_events = if can_merge { 0 } else { 1 };
        if num_events + num_new_events > events_limit {
            return false;
        }
        match self.pending_event {
            Some(_) => {
                if can_merge {
                    *self.events.back_mut().unwrap() = event;
                } else {
                    self.events.push_back(event);
                }
            }
            None => self.pending_event = Some(self.notifier.notify(event)),
        }
        true
    }

    fn next_event(&mut self) {
        self.pending_event = self.events.pop_front().map(|event| self.notifier.notify(event));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_test_pkg_eventqueue::{
        ExampleEventMonitorMarker, ExampleEventMonitorProxy, ExampleEventMonitorRequest,
        ExampleEventMonitorRequestStream,
    };
    use fuchsia_async as fasync;
    use matches::assert_matches;

    struct FidlNotifier {
        proxy: ExampleEventMonitorProxy,
    }

    impl Notify<String> for FidlNotifier {
        fn notify(&self, event: String) -> BoxFuture<'static, Result<(), ClosedClient>> {
            self.proxy.on_event(&event).map(|result| result.map_err(|_| ClosedClient)).boxed()
        }
    }

    struct MpscNotifier {
        sender: mpsc::Sender<String>,
    }

    impl Notify<String> for MpscNotifier {
        fn notify(&self, event: String) -> BoxFuture<'static, Result<(), ClosedClient>> {
            let mut sender = self.sender.clone();
            async move { sender.send(event).map(|result| result.map_err(|_| ClosedClient)).await }
                .boxed()
        }
    }

    impl Event for String {
        fn can_merge(&self, other: &String) -> bool {
            self == other
        }
    }

    fn start_event_queue() -> ControlHandle<FidlNotifier, String> {
        let (event_queue, handle) = EventQueue::<FidlNotifier, String>::new();
        fasync::spawn_local(event_queue);
        handle
    }

    async fn add_client(
        handle: &mut ControlHandle<FidlNotifier, String>,
    ) -> ExampleEventMonitorRequestStream {
        let (proxy, stream) = create_proxy_and_stream::<ExampleEventMonitorMarker>().unwrap();
        handle.add_client(FidlNotifier { proxy }).await.unwrap();
        stream
    }

    async fn assert_events(
        stream: &mut ExampleEventMonitorRequestStream,
        expected_events: &[&str],
    ) {
        for &expected_event in expected_events {
            match stream.try_next().await.unwrap().unwrap() {
                ExampleEventMonitorRequest::OnEvent { event, responder } => {
                    assert_eq!(&event, expected_event);
                    responder.send().unwrap();
                }
            }
        }
    }

    async fn assert_client_dropped(
        stream: &mut ExampleEventMonitorRequestStream,
        expected_event: &str,
    ) {
        match stream.try_next().await.unwrap().unwrap() {
            ExampleEventMonitorRequest::OnEvent { event, responder } => {
                assert_eq!(&event, expected_event);
                // Sending response will fail because the client was dropped.
                assert_matches!(responder.send(), Err(_));
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_simple() {
        let (event_queue, mut handle) = EventQueue::<MpscNotifier, String>::new();
        fasync::spawn_local(event_queue);
        let (sender, mut receiver) = mpsc::channel(1);
        handle.add_client(MpscNotifier { sender }).await.unwrap();
        handle.queue_event("event".into()).await.unwrap();
        assert_matches!(receiver.next().await.as_ref().map(|s| s.as_str()), Some("event"));
        drop(handle);
        assert_matches!(receiver.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_simple_fidl() {
        let mut handle = start_event_queue();
        let mut stream = add_client(&mut handle).await;
        handle.queue_event("event".into()).await.unwrap();
        assert_events(&mut stream, &["event"]).await;
        drop(handle);
        assert_matches!(stream.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_multi_client_multi_event() {
        let mut handle = start_event_queue();
        let mut stream1 = add_client(&mut handle).await;
        handle.queue_event("event1".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();

        let mut stream2 = add_client(&mut handle).await;
        handle.queue_event("event3".into()).await.unwrap();

        assert_events(&mut stream1, &["event1", "event2", "event3"]).await;
        assert_events(&mut stream2, &["event2", "event3"]).await;

        drop(handle);
        assert_matches!(stream1.next().await, None);
        assert_matches!(stream2.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_clear_clients() {
        let mut handle = start_event_queue();
        let mut stream1 = add_client(&mut handle).await;
        handle.queue_event("event1".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();
        handle.clear().await.unwrap();
        let mut stream2 = add_client(&mut handle).await;
        handle.queue_event("event3".into()).await.unwrap();
        assert_client_dropped(&mut stream1, "event1").await;
        // No event2 because the client was dropped before responding to event1.
        assert_matches!(stream1.next().await, None);

        assert_events(&mut stream2, &["event3"]).await;
        drop(handle);
        assert_matches!(stream2.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_drop_unresponsive_clients() {
        let mut handle = start_event_queue();
        let mut stream = add_client(&mut handle).await;
        for i in 1..12 {
            handle.queue_event(format!("event{}", i)).await.unwrap();
        }
        assert_client_dropped(&mut stream, "event1").await;
        assert_matches!(stream.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_drop_unresponsive_clients_custom_limit() {
        let (event_queue, mut handle) = EventQueue::<FidlNotifier, String>::with_limit(2);
        fasync::spawn_local(event_queue);
        let mut stream = add_client(&mut handle).await;

        handle.queue_event("event1".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();
        handle.queue_event("event3".into()).await.unwrap();
        assert_client_dropped(&mut stream, "event1").await;
        assert_matches!(stream.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_drop_unresponsive_clients_custom_limit_merge() {
        let (event_queue, mut handle) = EventQueue::<FidlNotifier, String>::with_limit(2);
        fasync::spawn_local(event_queue);
        let mut stream = add_client(&mut handle).await;

        handle.queue_event("event1".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();
        assert_events(&mut stream, &["event1", "event2"]).await;
        drop(handle);
        assert_matches!(stream.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_drop_failed_clients() {
        let mut handle = start_event_queue();
        let mut stream = add_client(&mut handle).await;
        handle.queue_event("event1".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();
        match stream.try_next().await.unwrap().unwrap() {
            ExampleEventMonitorRequest::OnEvent { event, .. } => {
                assert_eq!(event, "event1");
                // Don't respond.
            }
        }
        assert_matches!(stream.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_drop_failed_clients_multiple() {
        let mut handle = start_event_queue();
        let mut stream1 = add_client(&mut handle).await;
        let mut stream2 = add_client(&mut handle).await;
        handle.queue_event("event1".into()).await.unwrap();
        handle.queue_event("event2".into()).await.unwrap();
        match stream1.try_next().await.unwrap().unwrap() {
            ExampleEventMonitorRequest::OnEvent { event, .. } => {
                assert_eq!(event, "event1");
                // Don't respond.
            }
        }
        assert_matches!(stream1.next().await, None);
        // stream2 can still receive events.
        handle.queue_event("event3".into()).await.unwrap();
        assert_events(&mut stream2, &["event1", "event2", "event3"]).await;
        drop(handle);
        assert_matches!(stream2.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_event_queue_merge_events() {
        let mut handle = start_event_queue();
        let mut stream = add_client(&mut handle).await;
        for i in 0..10 {
            handle.queue_event(format!("event{}", i / 4)).await.unwrap();
        }
        // The first event won't be merged because it's already sent before the second event comes.
        assert_events(&mut stream, &["event0", "event0", "event1", "event2"]).await;
        drop(handle);
        assert_matches!(stream.next().await, None);
    }
}
