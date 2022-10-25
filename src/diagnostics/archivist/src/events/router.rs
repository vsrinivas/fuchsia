// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{events::types::*, identity::ComponentIdentity};
use async_trait::async_trait;
use fuchsia_inspect::{self as inspect, NumericProperty, StringReference};
use fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode};
use futures::{
    channel::{mpsc, oneshot},
    task::{Context, Poll},
    Future, SinkExt, Stream, StreamExt,
};
use lazy_static::lazy_static;
use pin_project::pin_project;
use std::{
    collections::{BTreeMap, BTreeSet},
    pin::Pin,
    sync::{Arc, Weak},
};
use thiserror::Error;
use tracing::{debug, error};

const MAX_EVENT_BUS_CAPACITY: usize = 1024;
const RECENT_EVENT_LIMIT: usize = 200;

lazy_static! {
    static ref EVENT: StringReference<'static> = "event".into();
    static ref MONIKER: StringReference<'static> = "moniker".into();
}

pub struct RouterOptions {
    /// Whether or not to validate that the event routing is complete: for each consumer of an
    /// event there exists at least one producer. And for each producer, there exists at least one
    /// consumer.
    pub validate: bool,
}

impl Default for RouterOptions {
    fn default() -> Self {
        Self { validate: true }
    }
}

/// Core archivist internal event router that supports multiple event producers and multiple event
/// consumers.
pub struct EventRouter {
    // All the consumers that have been registered for an event.
    consumers: BTreeMap<EventType, Vec<Weak<dyn EventConsumer + Send + Sync>>>,
    // The types of all events that can be produced. Used only for validation.
    producers_registered: BTreeSet<EventType>,

    // Ends of the channel used by event producers.
    sender: mpsc::Sender<Event>,
    receiver: mpsc::Receiver<Event>,

    inspect_logger: EventStreamLogger,
}

impl EventRouter {
    /// Creates a new empty event router.
    pub fn new(node: inspect::Node) -> Self {
        let (sender, receiver) = mpsc::channel(MAX_EVENT_BUS_CAPACITY);
        Self {
            consumers: BTreeMap::new(),
            sender,
            receiver,
            producers_registered: BTreeSet::new(),
            inspect_logger: EventStreamLogger::new(node),
        }
    }

    /// Registers an event producer with the given configuration specifying the types of events the
    /// given producer is allowed to emit.
    pub fn add_producer<T>(&mut self, config: ProducerConfig<'_, T>)
    where
        T: EventProducer,
    {
        let events: BTreeSet<_> = config.events.into_iter().collect();
        self.producers_registered.append(&mut events.clone());
        let dispatcher = Dispatcher::new(events, self.sender.clone());
        config.producer.set_dispatcher(dispatcher);
    }

    /// Registers an event consumer with the given configuration specifying the types of events the
    /// given consumer will receive.
    pub fn add_consumer<T: 'static>(&mut self, config: ConsumerConfig<'_, T>)
    where
        T: EventConsumer + Send + Sync,
    {
        let subscriber_weak = Arc::downgrade(config.consumer);
        for event_type in config.events {
            self.consumers.entry(event_type.into()).or_default().push(subscriber_weak.clone());
        }
    }

    /// Starts listening for events emitted by the registered producers and dispatching them to
    /// registered consumers.
    ///
    /// First, validates that for every event type that will be dispatched, there exists at least
    /// one consumer. And that for every event that will be consumed, there exists at least one
    /// producer.
    ///
    /// Afterwards, listens to events emitted by producers. When an event arrives it sends it to
    /// all consumers of the event. Since all events are singletons, the first consumer that was
    /// registered will get the singleton data and the rest won't.
    pub fn start(
        mut self,
        opts: RouterOptions,
    ) -> Result<(TerminateHandle, impl Future<Output = ()>), RouterError> {
        if opts.validate {
            self.validate_routing()?;
        }

        let (terminate_handle, mut stream) = EventStream::new(self.receiver);
        let mut consumers = self.consumers;
        let mut inspect_logger = self.inspect_logger;

        let fut = async move {
            loop {
                match stream.next().await {
                    None => {
                        debug!("Event ingestion finished");
                        break;
                    }
                    Some(event) => {
                        inspect_logger.log(&event);

                        let event_type = event.ty();
                        let weak_consumers = match consumers.get_mut(&event_type) {
                            Some(c) => c,
                            None => continue,
                        };

                        let event_without_singleton_data = event.clone();
                        let mut event_with_singleton_data = Some(event);

                        // Consumers which weak reference could be upgraded will be stored here.
                        let mut active_consumers = vec![];
                        for consumer in weak_consumers.iter_mut().filter_map(|c| c.upgrade()) {
                            active_consumers.push(Arc::downgrade(&consumer));
                            let e = event_with_singleton_data
                                .take()
                                .unwrap_or_else(|| event_without_singleton_data.clone());
                            consumer.handle(e).await;
                        }

                        // We insert the list of active consumers in the map at the key for this
                        // event type. This leads to dropping the previous list of weak references
                        // which contains consumers which aren't active anymore.
                        consumers.insert(event_type, active_consumers);
                    }
                }
            }
        };
        Ok((terminate_handle, fut))
    }

    fn validate_routing(&mut self) -> Result<(), RouterError> {
        for consumed_event in self.consumers.keys() {
            if self.producers_registered.get(consumed_event).is_none() {
                return Err(RouterError::MissingProducer(consumed_event.clone()));
            }
        }
        for produced_event in &self.producers_registered {
            if self.consumers.get(produced_event).is_none() {
                return Err(RouterError::MissingConsumer(produced_event.clone()));
            }
        }
        Ok(())
    }
}

/// Stream of events that  provides the mechanisms used to notify when the events have
/// been drained.
#[pin_project]
struct EventStream {
    /// The stream containing events.
    #[pin]
    receiver: mpsc::Receiver<Event>,

    /// When this future is ready, the stream will be closed. Messages still in the buffer
    /// will be drained.
    #[pin]
    on_terminate: oneshot::Receiver<()>,

    /// When the stream has been drained a notification will be sent through this channel.
    on_drained: Option<oneshot::Sender<()>>,
}

impl EventStream {
    fn new(receiver: mpsc::Receiver<Event>) -> (TerminateHandle, Self) {
        let (snd, rcv) = oneshot::channel();
        let (drain_snd, drain_rcv) = oneshot::channel();
        (
            TerminateHandle { snd, drained: drain_rcv },
            Self { receiver, on_terminate: rcv, on_drained: Some(drain_snd) },
        )
    }
}

impl Stream for EventStream {
    type Item = Event;

    /// This stream implementation merges two streams into a single one polling from each of them
    /// in a round robin fashion. When one stream finishes, this will keep polling from the
    /// remaining one.
    ///
    /// When receiving a request for termination, the event stream will be
    /// closed so that no new messages can be sent through that channel, but it'll still be drained.
    ///
    /// When the stream has been drained, a message is sent through the appropriate
    /// channel.
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut this = self.project();

        // First check if request to terminate the event ingestion has been requested, if
        // it has, then close the channel to which events are sent. This will prevent
        // further messages to be sent, but it remains possible to drain the channel
        // buffer.
        //
        // IMPORTANT: If we ever use this event stream for events emitted internally, then we
        // should bring back the changed undone in https://fxrev.dev/744413 as internal event
        // streams shouldn't be closed on termination.
        match this.on_terminate.poll(cx) {
            Poll::Pending => {}
            Poll::Ready(_) => {
                this.receiver.close();
            }
        }
        // Poll the stream and track whether it's drained or not.
        match this.receiver.poll_next(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(None) => {
                // Notify once that it has been drained.
                if let Some(snd) = this.on_drained.take() {
                    snd.send(()).unwrap_or_else(|err| {
                        error!(?err, "Failed to notify the events have been drained.");
                    });
                };
                Poll::Ready(None)
            }
            res @ Poll::Ready(Some(_)) => res,
        }
    }
}

/// Allows to termiante event ingestion.
pub struct TerminateHandle {
    snd: oneshot::Sender<()>,
    drained: oneshot::Receiver<()>,
}

impl TerminateHandle {
    /// Terminates event ingestion. Buffered events will be drained. The returned future
    /// will complete once all buffered events have been drained.
    pub async fn terminate(self) {
        self.snd.send(()).unwrap_or_else(|err| {
            error!(?err, "Failed to terminate the event ingestion.");
        });
        self.drained
            .await
            .unwrap_or_else(|err| error!(?err, "Error waiting for events to be drained."));
    }
}

/// Allows to emit events of a restricted set of types.
///
/// Event producers will receive a `Dispatcher` instance that will allow them to emit events of
/// restricted set of types.
pub struct Dispatcher {
    allowed_events: BTreeSet<EventType>,
    sender: Option<mpsc::Sender<Event>>,
}

/// Returns a no-op dispatcher.
impl Default for Dispatcher {
    fn default() -> Self {
        Self { allowed_events: BTreeSet::new(), sender: None }
    }
}

impl Dispatcher {
    fn new(allowed_events: BTreeSet<EventType>, sender: mpsc::Sender<Event>) -> Self {
        Self { allowed_events, sender: Some(sender) }
    }

    /// Emits an event. If the event isn't in the restricted set of allowed types, this operation
    /// is a no-op. An error is returned when sending the event into the channel fails.
    pub async fn emit(&mut self, event: Event) -> Result<(), mpsc::SendError> {
        if let Some(sender) = &mut self.sender {
            if self.allowed_events.contains(&event.ty()) {
                sender.send(event).await?;
            }
        }
        Ok(())
    }

    #[cfg(test)]
    pub fn new_for_test(allowed_events: BTreeSet<EventType>) -> (mpsc::Receiver<Event>, Self) {
        let (sender, receiver) = mpsc::channel(100);
        (receiver, Self::new(allowed_events, sender))
    }
}

struct EventStreamLogger {
    counters: BTreeMap<EventType, inspect::UintProperty>,
    component_log_node: BoundedListNode,
    counters_node: inspect::Node,
    _node: inspect::Node,
}

impl EventStreamLogger {
    /// Creates a new event logger. All inspect data will be written as children of `parent`.
    pub fn new(node: inspect::Node) -> Self {
        let counters_node = node.create_child("event_counts");
        let recent_events_node = node.create_child("recent_events");
        Self {
            _node: node,
            counters: BTreeMap::new(),
            counters_node,
            component_log_node: BoundedListNode::new(recent_events_node, RECENT_EVENT_LIMIT),
        }
    }

    /// Log a new component event to inspect.
    pub fn log(&mut self, event: &Event) {
        let ty = event.ty();
        if self.counters.contains_key(&ty) {
            self.counters.get_mut(&ty).unwrap().add(1);
        } else {
            let counter = self.counters_node.create_uint(ty.as_ref(), 1);
            self.counters.insert(ty.clone(), counter);
        }
        // TODO(fxbug.dev/92374): leverage string references for the payload.
        match &event.payload {
            EventPayload::DiagnosticsReady(DiagnosticsReadyPayload { component, .. })
            | EventPayload::LogSinkRequested(LogSinkRequestedPayload { component, .. }) => {
                self.log_inspect(ty.as_ref(), component);
            }
        }
    }

    fn log_inspect(&mut self, event_name: &str, identity: &ComponentIdentity) {
        // TODO(fxbug.dev/92374): leverage string references for the `event_name`.
        inspect_log!(self.component_log_node,
            &*EVENT => event_name,
            &*MONIKER => match &identity.instance_id {
                Some(instance_id) => format!("{}:{}", identity.relative_moniker, instance_id),
                None => identity.relative_moniker.to_string(),
            }
        );
    }
}

/// Set of errors that can happen when setting up an event router and executing its dispatching loop.
#[derive(Debug, Error)]
pub enum RouterError {
    #[error("Missing consumer for event type {0:?}")]
    MissingConsumer(EventType),

    #[error("Missing producer for event type {0:?}")]
    MissingProducer(EventType),
}

/// Configuration for an event producer.
pub struct ProducerConfig<'a, T> {
    /// The event producer that will receive a `Dispatcher`
    pub producer: &'a mut T,

    /// The set of events that the `producer` will be allowed to emit.
    pub events: Vec<EventType>,
}

/// Configuration for an event consumer.
pub struct ConsumerConfig<'a, T> {
    /// The event consumer that will receive events when they are emitted by producers.
    pub consumer: &'a Arc<T>,

    /// The set of event types that the `consumer` will receive.
    pub events: Vec<EventType>,
}

/// Trait implemented by data types which receive events.
#[async_trait]
pub trait EventConsumer {
    /// Event consumers will receive a call on this method when an event they are interested on
    /// happens.
    async fn handle(self: Arc<Self>, event: Event);
}

/// Trait implemented by data types which emit events.
pub trait EventProducer {
    /// Whent registered, event producers will receive a call on this method with the `dispatcher`
    /// they can use to emit events.
    fn set_dispatcher(&mut self, dispatcher: Dispatcher);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::events::types::ComponentIdentifier;
    use assert_matches::assert_matches;
    use fidl_fuchsia_logger::LogSinkMarker;
    use fuchsia_async as fasync;
    use fuchsia_inspect::assert_data_tree;
    use fuchsia_zircon as zx;
    use futures::{lock::Mutex, FutureExt};
    use lazy_static::lazy_static;

    const TEST_URL: &'static str = "NO-OP URL";
    const FAKE_TIMESTAMP: i64 = 5;
    lazy_static! {
        static ref IDENTITY: ComponentIdentity = ComponentIdentity::from_identifier_and_url(
            ComponentIdentifier::parse_from_moniker("./a/b").unwrap(),
            TEST_URL
        );
        static ref LEGACY_IDENTITY: ComponentIdentity = ComponentIdentity::from_identifier_and_url(
            ComponentIdentifier::Legacy {
                instance_id: "12345".to_string(),
                moniker: vec!["a", "b", "foo.cmx"].into(),
            },
            &*TEST_URL
        );
    }

    #[derive(Default)]
    struct TestEventProducer {
        dispatcher: Dispatcher,
    }

    impl TestEventProducer {
        async fn emit(&mut self, event_type: EventType, identity: ComponentIdentity) {
            let event = match event_type {
                EventType::DiagnosticsReady => Event {
                    timestamp: zx::Time::from_nanos(FAKE_TIMESTAMP),
                    payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                        component: identity,
                        directory: None,
                    }),
                },
                EventType::LogSinkRequested => Event {
                    timestamp: zx::Time::from_nanos(FAKE_TIMESTAMP),
                    payload: EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                        component: identity,
                        request_stream: None,
                    }),
                },
            };
            let _ = self.dispatcher.emit(event).await;
        }
    }

    impl EventProducer for TestEventProducer {
        fn set_dispatcher(&mut self, dispatcher: Dispatcher) {
            self.dispatcher = dispatcher;
        }
    }

    struct TestEventConsumer {
        event_sender: Mutex<mpsc::Sender<Event>>,
    }

    impl TestEventConsumer {
        fn new() -> (mpsc::Receiver<Event>, Arc<Self>) {
            let (event_sender, event_receiver) = mpsc::channel(10);
            (event_receiver, Arc::new(Self { event_sender: Mutex::new(event_sender) }))
        }
    }

    #[async_trait]
    impl EventConsumer for TestEventConsumer {
        async fn handle(self: Arc<Self>, event: Event) {
            self.event_sender.lock().await.send(event).await.unwrap();
        }
    }

    #[fuchsia::test]
    async fn invalid_routing() {
        let mut producer = TestEventProducer::default();
        let (_receiver, consumer) = TestEventConsumer::new();
        let mut router = EventRouter::new(inspect::Node::default());
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::DiagnosticsReady, EventType::LogSinkRequested],
        });

        // An explicit match is needed here since unwrap_err requires Debug implemented for both T
        // and E in Result<T, E> and T is a pair which second element is `impl Future` which
        // doesn't implement Debug.
        match router.start(RouterOptions::default()) {
            Err(err) => {
                assert_matches!(err, RouterError::MissingProducer(EventType::LogSinkRequested));
            }
            Ok(_) => panic!("expected an error from routing events"),
        }

        let mut producer = TestEventProducer::default();
        let (_receiver, consumer) = TestEventConsumer::new();
        let mut router = EventRouter::new(inspect::Node::default());
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::LogSinkRequested],
        });

        match router.start(RouterOptions::default()) {
            Err(err) => {
                assert_matches!(
                    err,
                    RouterError::MissingConsumer(EventType::DiagnosticsReady)
                        | RouterError::MissingProducer(EventType::LogSinkRequested)
                );
            }
            Ok(_) => panic!("expected an error from routing events"),
        }
    }

    #[fuchsia::test]
    async fn event_subscription() {
        let mut producer = TestEventProducer::default();
        let (mut first_receiver, first_consumer) = TestEventConsumer::new();
        let (mut second_receiver, second_consumer) = TestEventConsumer::new();
        let mut router = EventRouter::new(inspect::Node::default());
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            events: vec![EventType::LogSinkRequested],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &first_consumer,
            events: vec![EventType::LogSinkRequested],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &second_consumer,
            events: vec![EventType::LogSinkRequested],
        });

        let (_terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);

        // Emit an event
        let (_, request_stream) =
            fidl::endpoints::create_request_stream::<LogSinkMarker>().unwrap();
        let timestamp = zx::Time::get_monotonic();
        producer
            .dispatcher
            .emit(Event {
                timestamp: timestamp.clone(),
                payload: EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                    component: IDENTITY.clone(),
                    request_stream: Some(request_stream),
                }),
            })
            .await
            .unwrap();

        // The first consumer that was registered must receive the request stream. The second one
        // must receive no payload, but still receive the event.
        let first_event = first_receiver.next().await.unwrap();
        assert_matches!(first_event, Event {
            payload: EventPayload::LogSinkRequested(payload),
            ..
        } => {
            assert_eq!(payload.component, *IDENTITY);
            assert!(payload.request_stream.is_some());
        });
        let second_event = second_receiver.next().await.unwrap();
        assert_matches!(second_event, Event {
            payload: EventPayload::LogSinkRequested(payload),
            ..
        } => {
            assert_eq!(payload.component, *IDENTITY);
            assert!(payload.request_stream.is_none());
        });
    }

    #[fuchsia::test]
    async fn consumers_cleanup() {
        let mut producer = TestEventProducer::default();
        let (mut first_receiver, first_consumer) = TestEventConsumer::new();
        let (mut second_receiver, second_consumer) = TestEventConsumer::new();
        let (mut third_receiver, third_consumer) = TestEventConsumer::new();
        let mut router = EventRouter::new(inspect::Node::default());
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &first_consumer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &second_consumer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &third_consumer,
            events: vec![EventType::DiagnosticsReady],
        });

        drop(first_consumer);
        drop(third_consumer);

        let (_terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);

        // Emit an event
        producer
            .dispatcher
            .emit(Event {
                timestamp: zx::Time::get_monotonic(),
                payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                    component: IDENTITY.clone(),
                    directory: None,
                }),
            })
            .await
            .unwrap();

        // We see the event only in the receiver which consumer wasn't dropped.
        let event = second_receiver.next().await.unwrap();
        assert_matches!(event.payload, EventPayload::DiagnosticsReady(_));
        assert!(first_receiver.next().now_or_never().unwrap().is_none());
        assert!(third_receiver.next().now_or_never().unwrap().is_none());

        // We see additional events in the second receiver which remains alive.
        producer
            .dispatcher
            .emit(Event {
                timestamp: zx::Time::get_monotonic(),
                payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                    component: IDENTITY.clone(),
                    directory: None,
                }),
            })
            .await
            .unwrap();
        let event = second_receiver.next().await.unwrap();
        assert_matches!(event.payload, EventPayload::DiagnosticsReady(_));
        assert!(first_receiver.next().now_or_never().unwrap().is_none());
        assert!(third_receiver.next().now_or_never().unwrap().is_none());
    }

    #[fuchsia::test]
    async fn inspect_log() {
        let inspector = inspect::Inspector::new();
        let mut router = EventRouter::new(inspector.root().create_child("events"));
        let mut producer1 = TestEventProducer::default();
        let mut producer2 = TestEventProducer::default();
        let (receiver, consumer) = TestEventConsumer::new();
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::LogSinkRequested, EventType::DiagnosticsReady],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer1,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer2,
            events: vec![EventType::LogSinkRequested],
        });

        producer1.emit(EventType::DiagnosticsReady, LEGACY_IDENTITY.clone()).await;
        producer2.emit(EventType::LogSinkRequested, IDENTITY.clone()).await;

        // Consume the events.
        let (_terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);
        receiver.take(2).collect::<Vec<_>>().await;

        assert_data_tree!(inspector, root: {
            events: {
                event_counts: {
                    diagnostics_ready: 1u64,
                    log_sink_requested: 1u64
                },
                recent_events: {
                    "0": {
                        "@time": inspect::testing::AnyProperty,
                        event: "diagnostics_ready",
                        moniker: "a/b/foo.cmx:12345"
                    },
                    "1": {
                        "@time": inspect::testing::AnyProperty,
                        event: "log_sink_requested",
                        moniker: "a/b"
                    },
                }
            }
        });
    }

    #[fuchsia::test]
    async fn event_stream_semantics() {
        let inspector = inspect::Inspector::new();
        let mut router = EventRouter::new(inspector.root().create_child("events"));
        let mut producer1 = TestEventProducer::default();
        let mut producer2 = TestEventProducer::default();
        let (receiver, consumer) = TestEventConsumer::new();
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer1,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer2,
            events: vec![EventType::DiagnosticsReady],
        });

        let identity = |moniker| {
            ComponentIdentity::from_identifier_and_url(
                ComponentIdentifier::parse_from_moniker(moniker).unwrap(),
                TEST_URL,
            )
        };

        producer1.emit(EventType::DiagnosticsReady, identity("./a")).await;
        producer2.emit(EventType::DiagnosticsReady, identity("./b")).await;
        producer1.emit(EventType::DiagnosticsReady, identity("./c")).await;
        producer2.emit(EventType::DiagnosticsReady, identity("./d")).await;

        // We should see the events in order of emission.
        let (_terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);
        let events = receiver.take(4).collect::<Vec<_>>().await;

        let expected_events = vec![
            diagnostics_ready(identity("./a")),
            diagnostics_ready(identity("./b")),
            diagnostics_ready(identity("./c")),
            diagnostics_ready(identity("./d")),
        ];
        assert_eq!(events.len(), expected_events.len());
        for (event, expected_event) in std::iter::zip(events, expected_events) {
            assert_event(event, expected_event);
        }
    }

    #[fuchsia::test]
    async fn stream_draining() {
        let inspector = inspect::Inspector::new();
        let mut router = EventRouter::new(inspector.root().create_child("events"));
        let mut producer = TestEventProducer::default();
        let (mut receiver, consumer) = TestEventConsumer::new();
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            events: vec![EventType::DiagnosticsReady],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            events: vec![EventType::DiagnosticsReady],
        });

        producer.emit(EventType::DiagnosticsReady, IDENTITY.clone()).await;

        let (terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);
        let on_drained = terminate_handle.terminate();
        let drain_finished = fasync::Task::spawn(async move { on_drained.await });

        assert_event(receiver.next().await.unwrap(), diagnostics_ready(IDENTITY.clone()));

        // This future must be complete now.
        drain_finished.await;

        // We must never see any new event emitted by the producer.
        producer.emit(EventType::DiagnosticsReady, IDENTITY.clone()).await;
        assert!(receiver.next().now_or_never().is_none());
    }

    fn assert_event(event: Event, other: Event) {
        assert_eq!(event.timestamp, other.timestamp);
        match (event.payload, other.payload) {
            (
                EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                    component: this_identity,
                    ..
                }),
                EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                    component: other_identity,
                    ..
                }),
            ) => {
                assert_eq!(this_identity, other_identity);
            }
            _ => unimplemented!("no other combinations are expected in these tests"),
        }
    }

    fn diagnostics_ready(identity: ComponentIdentity) -> Event {
        Event {
            timestamp: zx::Time::from_nanos(FAKE_TIMESTAMP),
            payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                component: identity,
                directory: None,
            }),
        }
    }
}
