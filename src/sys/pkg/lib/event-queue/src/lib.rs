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

use {
    fuchsia_async::TimeoutExt,
    futures::{
        channel::mpsc,
        future::{select_all, BoxFuture},
        prelude::*,
        select,
    },
    std::{collections::VecDeque, time::Duration},
    thiserror::Error,
};

mod barrier;
use barrier::{Barrier, BarrierBlock};

const DEFAULT_EVENTS_LIMIT: usize = 10;

/// The event type need to implement this trait to tell the event queue whether two consecutive
/// pending events can be merged into a single event, if `can_merge` returns true, the event queue
/// will replace the last event in the queue with the latest event.
pub trait Event: Clone {
    /// Returns whether this event can be merged with another event.
    fn can_merge(&self, other: &Self) -> bool;
}

/// The client is closed and should be removed from the event queue.
#[derive(Debug, Error, PartialEq, Eq)]
#[error("The client is closed and should be removed from the event queue.")]
pub struct ClosedClient;

/// The event queue future was dropped before calling control handle functions.
#[derive(Debug, Error, PartialEq, Eq)]
#[error("The event queue future was dropped before calling control handle functions.")]
pub struct EventQueueDropped;

/// The flush operation timed out.
#[derive(Debug, Error, PartialEq, Eq)]
#[error("The flush operation timed out.")]
pub struct TimedOut;

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

#[derive(Debug)]
enum Command<N, E> {
    AddClient(N),
    Clear,
    QueueEvent(E),
    TryFlush(BarrierBlock),
}

/// A control handle that can control the event queue.
#[derive(Debug)]
pub struct ControlHandle<N, E>
where
    N: Notify<E>,
    E: Event,
{
    sender: mpsc::Sender<Command<N, E>>,
}

impl<N, E> Clone for ControlHandle<N, E>
where
    N: Notify<E>,
    E: Event,
{
    fn clone(&self) -> Self {
        ControlHandle { sender: self.sender.clone() }
    }
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

    /// Make all existing clients in the event queue stop adding new events to their queue, and
    /// once they receive all queued events, they will be dropped.
    pub async fn clear(&mut self) -> Result<(), EventQueueDropped> {
        self.sender.send(Command::Clear).await.map_err(|_| EventQueueDropped)
    }

    /// Queue an event that will be sent to all clients.
    pub async fn queue_event(&mut self, event: E) -> Result<(), EventQueueDropped> {
        self.sender.send(Command::QueueEvent(event)).await.map_err(|_| EventQueueDropped)
    }

    /// Try to flush all pending events to all connected clients, returning a future that completes
    /// once all events are flushed or the given timeout duration is reached.
    pub async fn try_flush(
        &mut self,
        timeout: Duration,
    ) -> Result<impl Future<Output = Result<(), TimedOut>>, EventQueueDropped> {
        let (barrier, block) = Barrier::new();
        let () = self.sender.send(Command::TryFlush(block)).await.map_err(|_| EventQueueDropped)?;

        Ok(barrier.map(Ok).on_timeout(timeout, || Err(TimedOut)))
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
                        Ok(()) => self.next_event(i),
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
                        Some(Command::TryFlush(block)) => self.try_flush(block),
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

    // Remove clients that have no pending events, otherwise tell them to not accept any new events.
    fn clear(&mut self) {
        let mut i = 0;
        while i < self.clients.len() {
            if self.clients[i].pending_event.is_none() {
                self.clients.swap_remove(i);
            } else {
                self.clients[i].accept_new_events = false;
                i += 1;
            }
        }
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

    fn try_flush(&mut self, block: BarrierBlock) {
        for client in self.clients.iter_mut() {
            client.queue_flush_notify(&block);
        }
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

    fn next_event(&mut self, i: usize) {
        self.clients[i].ack_event();
        if !self.clients[i].accept_new_events && self.clients[i].pending_event.is_none() {
            self.clients.swap_remove(i);
        }
    }
}

struct Client<N, E>
where
    N: Notify<E>,
    E: Event,
{
    notifier: N,
    pending_event: Option<BoxFuture<'static, Result<(), ClosedClient>>>,
    commands: VecDeque<ClientCommand<E>>,
    accept_new_events: bool,
}

impl<N, E> Client<N, E>
where
    N: Notify<E>,
    E: Event,
{
    fn new(notifier: N) -> Self {
        Client { notifier, pending_event: None, commands: VecDeque::new(), accept_new_events: true }
    }

    /// Returns the count of in-flight and queued events.
    fn pending_event_count(&self) -> usize {
        let queued_events = self.commands.iter().filter_map(ClientCommand::event).count();
        let pending_event = if self.pending_event.is_some() { 1 } else { 0 };

        queued_events + pending_event
    }

    /// Find the most recently queued event, if one exists.
    fn newest_queued_event(&mut self) -> Option<&mut E> {
        self.commands.iter_mut().rev().find_map(ClientCommand::event_mut)
    }

    fn queue_event(&mut self, event: E, events_limit: usize) -> bool {
        // Silently ignore new events if this client is part of a cleared session.
        if !self.accept_new_events {
            return true;
        }

        // Merge this new event with the most recent event, if one exists and is mergable.
        if let Some(newest_mergable_event) =
            self.newest_queued_event().filter(|last_event| last_event.can_merge(&event))
        {
            *newest_mergable_event = event;
            return true;
        }

        // If the event can't be merged, make sure this event isn't the 1 to exceed the limit.
        if self.pending_event_count() + 1 > events_limit {
            return false;
        }

        // Enqueue the event and, if one is not in-flight, dispatch it.
        self.queue_command(ClientCommand::SendEvent(event));
        true
    }

    /// Drop block once all prior events are sent/acked.
    fn queue_flush_notify(&mut self, block: &BarrierBlock) {
        // A cleared client is not part of this flush request.
        if !self.accept_new_events {
            return;
        }

        self.queue_command(ClientCommand::NotifyFlush(block.clone()));
    }

    /// Mark the in-flight event as acknowledged and process items from the queue.
    fn ack_event(&mut self) {
        self.pending_event = None;
        self.process_queue();
    }

    /// Enqueue the command for processing, and, if possible, process items from the queue.
    fn queue_command(&mut self, cmd: ClientCommand<E>) {
        self.commands.push_back(cmd);
        if self.pending_event.is_none() {
            self.process_queue();
        }
    }

    /// Assuming that no event is in-flight, process items from the queue until one is or the queue
    /// is empty.
    fn process_queue(&mut self) {
        assert!(self.pending_event.is_none());

        while let Some(event) = self.commands.pop_front() {
            match event {
                ClientCommand::SendEvent(event) => {
                    self.pending_event = Some(self.notifier.notify(event));
                    return;
                }
                ClientCommand::NotifyFlush(block) => {
                    // drop the block handle to indicate this client has flushed all prior events.
                    drop(block);
                }
            }
        }
    }
}

enum ClientCommand<E> {
    SendEvent(E),
    NotifyFlush(BarrierBlock),
}

impl<E> ClientCommand<E> {
    fn event(&self) -> Option<&E> {
        match self {
            ClientCommand::SendEvent(event) => Some(event),
            ClientCommand::NotifyFlush(_) => None,
        }
    }
    fn event_mut(&mut self) -> Option<&mut E> {
        match self {
            ClientCommand::SendEvent(event) => Some(event),
            ClientCommand::NotifyFlush(_) => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_test_pkg_eventqueue::{
            ExampleEventMonitorMarker, ExampleEventMonitorProxy, ExampleEventMonitorRequest,
            ExampleEventMonitorRequestStream,
        },
        fuchsia_async as fasync,
        futures::{pin_mut, task::Poll},
        matches::assert_matches,
    };

    struct FidlNotifier {
        proxy: ExampleEventMonitorProxy,
    }

    impl Notify<String> for FidlNotifier {
        fn notify(&self, event: String) -> BoxFuture<'static, Result<(), ClosedClient>> {
            self.proxy.on_event(&event).map(|result| result.map_err(|_| ClosedClient)).boxed()
        }
    }

    struct MpscNotifier<T> {
        sender: mpsc::Sender<T>,
    }

    impl<T> Notify<T> for MpscNotifier<T>
    where
        T: Event + Send + 'static,
    {
        fn notify(&self, event: T) -> BoxFuture<'static, Result<(), ClosedClient>> {
            let mut sender = self.sender.clone();
            async move { sender.send(event).map(|result| result.map_err(|_| ClosedClient)).await }
                .boxed()
        }
    }

    impl Event for &'static str {
        fn can_merge(&self, other: &&'static str) -> bool {
            self == other
        }
    }

    impl Event for String {
        fn can_merge(&self, other: &String) -> bool {
            self == other
        }
    }

    fn start_event_queue() -> ControlHandle<FidlNotifier, String> {
        let (event_queue, handle) = EventQueue::<FidlNotifier, String>::new();
        fasync::Task::local(event_queue).detach();
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
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, String>::new();
        fasync::Task::local(event_queue).detach();
        let (sender, mut receiver) = mpsc::channel(1);
        handle.add_client(MpscNotifier { sender }).await.unwrap();
        handle.queue_event("event".into()).await.unwrap();
        assert_matches!(receiver.next().await.as_ref().map(|s| s.as_str()), Some("event"));
        drop(handle);
        assert_matches!(receiver.next().await, None);
    }

    #[test]
    fn flush_with_no_clients_completes_immediately() {
        let mut executor = fasync::Executor::new().unwrap();
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, String>::new();
        let _event_queue = fasync::Task::local(event_queue);

        let wait_flush =
            executor.run_singlethreaded(handle.try_flush(Duration::from_secs(1))).unwrap();
        pin_mut!(wait_flush);

        assert_eq!(executor.run_until_stalled(&mut wait_flush), Poll::Ready(Ok(())));
    }

    #[test]
    fn flush_with_no_pending_events_completes_immediately() {
        let mut executor = fasync::Executor::new().unwrap();
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, String>::new();
        let _event_queue = fasync::Task::local(event_queue);

        let (sender, _receiver) = mpsc::channel(0);
        let wait_flush = executor.run_singlethreaded(async {
            handle.add_client(MpscNotifier { sender }).await.unwrap();
            handle.try_flush(Duration::from_secs(1)).await.unwrap()
        });
        pin_mut!(wait_flush);

        assert_eq!(executor.run_until_stalled(&mut wait_flush), Poll::Ready(Ok(())));
    }

    #[test]
    fn flush_with_pending_events_completes_once_events_are_flushed() {
        let mut executor = fasync::Executor::new().unwrap();
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, &'static str>::new();
        let _event_queue = fasync::Task::local(event_queue);

        let (sender1, mut receiver1) = mpsc::channel(0);
        let (sender2, mut receiver2) = mpsc::channel(0);
        let wait_flush = executor.run_singlethreaded(async {
            handle.add_client(MpscNotifier { sender: sender1 }).await.unwrap();
            handle.queue_event("first").await.unwrap();
            handle.queue_event("second").await.unwrap();
            handle.add_client(MpscNotifier { sender: sender2 }).await.unwrap();
            let wait_flush = handle.try_flush(Duration::from_secs(1)).await.unwrap();
            handle.queue_event("third").await.unwrap();
            wait_flush
        });
        pin_mut!(wait_flush);

        // No events acked yet, so the flush is pending.
        assert_eq!(executor.run_until_stalled(&mut wait_flush), Poll::Pending);

        // Some, but not all events acked, so the flush is still pending.
        let () = executor.run_singlethreaded(async {
            assert_eq!(receiver1.next().await, Some("first"));
        });
        assert_eq!(executor.run_until_stalled(&mut wait_flush), Poll::Pending);

        // All events prior to the flush now acked, so the flush is done.
        let () = executor.run_singlethreaded(async {
            assert_eq!(receiver1.next().await, Some("second"));
            assert_eq!(receiver2.next().await, Some("second"));
        });
        assert_eq!(executor.run_until_stalled(&mut wait_flush), Poll::Ready(Ok(())));
    }

    #[test]
    fn flush_with_pending_events_fails_at_timeout() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, &'static str>::new();
        let _event_queue = fasync::Task::local(event_queue);

        let (sender, mut receiver) = mpsc::channel(0);
        let wait_flush = {
            let setup = async {
                handle.queue_event("first").await.unwrap();
                handle.add_client(MpscNotifier { sender }).await.unwrap();
                handle.try_flush(Duration::from_secs(1)).await.unwrap()
            };
            pin_mut!(setup);
            match executor.run_until_stalled(&mut setup) {
                Poll::Ready(res) => res,
                _ => panic!(),
            }
        };
        pin_mut!(wait_flush);

        assert!(!executor.wake_expired_timers());
        assert_eq!(executor.run_until_stalled(&mut wait_flush), Poll::Pending);

        executor.set_fake_time(fasync::Time::after(Duration::from_secs(1).into()));
        assert!(executor.wake_expired_timers());
        assert_eq!(executor.run_until_stalled(&mut wait_flush), Poll::Ready(Err(TimedOut)));

        // A flush timing out does not otherwise affect the queue.
        let teardown = async {
            drop(handle);
            assert_eq!(receiver.next().await, Some("first"));
            assert_eq!(receiver.next().await, None);
        };
        pin_mut!(teardown);
        match executor.run_until_stalled(&mut teardown) {
            Poll::Ready(()) => {}
            _ => panic!(),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn flush_only_applies_to_active_clients() {
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, &'static str>::new();
        let _event_queue = fasync::Task::local(event_queue);

        let (sender1, mut receiver1) = mpsc::channel(0);
        let (sender2, mut receiver2) = mpsc::channel(0);

        handle.add_client(MpscNotifier { sender: sender1 }).await.unwrap();
        handle.queue_event("first").await.unwrap();
        handle.clear().await.unwrap();

        handle.add_client(MpscNotifier { sender: sender2 }).await.unwrap();
        handle.queue_event("second").await.unwrap();
        let flush = handle.try_flush(Duration::from_secs(1)).await.unwrap();

        // The flush completes even though the first cleared client hasn't acked its event yet.
        assert_eq!(receiver2.next().await, Some("second"));
        flush.await.unwrap();

        // Clear out all clients.
        assert_eq!(receiver1.next().await, Some("first"));
        assert_eq!(receiver1.next().await, None);
        handle.clear().await.unwrap();
        assert_eq!(receiver2.next().await, None);

        // Nop flush is fast.
        handle.try_flush(Duration::from_secs(1)).await.unwrap().await.unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn notify_flush_commands_do_not_count_towards_limit() {
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, &'static str>::with_limit(2);
        let _event_queue = fasync::Task::local(event_queue);

        let (sender1, mut receiver1) = mpsc::channel(0);
        let (sender2, mut receiver2) = mpsc::channel(0);

        // client 1 will reach the event limit
        handle.add_client(MpscNotifier { sender: sender1 }).await.unwrap();
        let flush1 = handle.try_flush(Duration::from_secs(1)).await.unwrap();
        handle.queue_event("event1").await.unwrap();
        handle.queue_event("event2").await.unwrap();

        // client 2 will not
        handle.add_client(MpscNotifier { sender: sender2 }).await.unwrap();
        let flush2 = handle.try_flush(Duration::from_secs(1)).await.unwrap();
        handle.queue_event("event3").await.unwrap();
        let flush3 = handle.try_flush(Duration::from_secs(1)).await.unwrap();

        flush1.await.unwrap();
        assert_eq!(receiver1.next().await, Some("event1"));
        assert_eq!(receiver1.next().await, None);

        assert_eq!(receiver2.next().await, Some("event2"));
        assert_eq!(receiver2.next().await, Some("event3"));

        flush2.await.unwrap();
        flush3.await.unwrap();

        handle.queue_event("event4").await.unwrap();

        assert_eq!(receiver2.next().await, Some("event4"));
        drop(handle);
        assert_eq!(receiver2.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn notify_flush_commands_do_not_interfere_with_event_merge() {
        let (event_queue, mut handle) = EventQueue::<MpscNotifier<_>, &'static str>::new();
        let _event_queue = fasync::Task::local(event_queue);

        let (sender, mut receiver) = mpsc::channel(0);

        handle.add_client(MpscNotifier { sender }).await.unwrap();
        handle.queue_event("first").await.unwrap();
        handle.queue_event("second_merge").await.unwrap();
        let wait_flush = handle.try_flush(Duration::from_secs(1)).await.unwrap();
        handle.queue_event("second_merge").await.unwrap();
        handle.queue_event("third").await.unwrap();

        assert_eq!(receiver.next().await, Some("first"));
        assert_eq!(receiver.next().await, Some("second_merge"));
        wait_flush.await.unwrap();
        assert_eq!(receiver.next().await, Some("third"));
        drop(handle);
        assert_eq!(receiver.next().await, None);
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
        assert_events(&mut stream2, &["event3"]).await;
        assert_events(&mut stream1, &["event1", "event2"]).await;
        // No event3 because the event queue was cleared.
        assert_matches!(stream1.next().await, None);

        // client2 should have no pending events and be dropped.
        handle.clear().await.unwrap();
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
        fasync::Task::local(event_queue).detach();
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
        fasync::Task::local(event_queue).detach();
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
