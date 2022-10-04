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
    iter::Extend,
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
    consumers: BTreeMap<AnyEventType, Vec<Weak<dyn EventConsumer + Send + Sync>>>,
    // The types of all events that can be produced. Used only for validation.
    producers_registered: BTreeSet<AnyEventType>,

    // Ends of the channel used by internal event producers.
    internal_sender: mpsc::Sender<Event>,
    internal_receiver: mpsc::Receiver<Event>,

    // Ends of the channel used by all external event producers.
    external_sender: mpsc::Sender<Event>,
    external_receiver: mpsc::Receiver<Event>,

    inspect_logger: EventStreamLogger,
}

impl EventRouter {
    /// Creates a new empty event router.
    pub fn new(node: inspect::Node) -> Self {
        let (internal_sender, internal_receiver) = mpsc::channel(MAX_EVENT_BUS_CAPACITY);
        let (external_sender, external_receiver) = mpsc::channel(MAX_EVENT_BUS_CAPACITY);
        Self {
            consumers: BTreeMap::new(),
            internal_sender,
            internal_receiver,
            external_sender,
            external_receiver,
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
        let mut events: BTreeSet<_> = config.events.into_iter().map(|e| e.into()).collect();
        events.extend(config.singleton_events.into_iter().map(|e| e.into()));
        self.producers_registered.append(&mut events.clone());
        let sender = match config.producer_type {
            ProducerType::Internal => self.internal_sender.clone(),
            ProducerType::External => self.external_sender.clone(),
        };
        let dispatcher = Dispatcher::new(events, sender);
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
        for event_type in config.singleton_events {
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
    /// all consumers of the event. If the event is singleton, the first consumer that was
    /// registered will get the singleton data and the rest won't.
    pub fn start(
        mut self,
        opts: RouterOptions,
    ) -> Result<(TerminateHandle, impl Future<Output = ()>), RouterError> {
        if opts.validate {
            self.validate_routing()?;
        }

        let (terminate_handle, mut stream) =
            EventStream::new(self.external_receiver, self.internal_receiver);
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
                        let mut event_with_singleton_data =
                            if event.is_singleton() { Some(event) } else { None };

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

/// Stream of events that merges the internal and external stream into a single stream. It also
/// provides the mechanisms used to notify when the external events have been drained.
#[pin_project]
struct EventStream {
    /// The stream containing events originating externally.
    #[pin]
    external: mpsc::Receiver<Event>,

    /// The stream conitaining events originating internally.
    #[pin]
    internal: mpsc::Receiver<Event>,

    /// When this future is ready, the external stream will be closed. Messages still in the buffer
    /// will be drained.
    #[pin]
    on_terminate: oneshot::Receiver<()>,

    /// When the external stream has been drained a notification will be sent through this channel.
    on_external_drained: Option<oneshot::Sender<()>>,

    /// Specifies what stream will be polled first. When true, the external stream is polled first,
    /// when false, the internal stream is polled first. Polling of both streams will be alteranted
    /// in a round robin fashion.
    turn: Turn,
}

enum Turn {
    Internal,
    External,
}

impl Turn {
    fn advance(&mut self) {
        match self {
            Turn::Internal => *self = Turn::External,
            Turn::External => *self = Turn::Internal,
        }
    }
}

impl EventStream {
    fn new(
        external: mpsc::Receiver<Event>,
        internal: mpsc::Receiver<Event>,
    ) -> (TerminateHandle, Self) {
        let (snd, rcv) = oneshot::channel();
        let (external_drain_snd, external_drain_rcv) = oneshot::channel();
        (
            TerminateHandle { snd, external_drained: external_drain_rcv },
            Self {
                external,
                internal,
                on_terminate: rcv,
                on_external_drained: Some(external_drain_snd),
                turn: Turn::External,
            },
        )
    }
}

impl Stream for EventStream {
    type Item = Event;

    /// This stream implementation merges two streams into a single one polling from each of them
    /// in a round robin fashion. When one stream finishes, this will keep polling from the
    /// remaining one.
    ///
    /// When receiving a request for termination, the external event stream will be
    /// closed so that no new messages can be sent through that channel, but it'll still be drained.
    ///
    /// When the external stream has been drained, a message is sent through the appropriate
    /// channel.
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut this = self.project();

        // First check if request to terminate the external event ingestion has been requested, if
        // it has, then close the channel to which external events are sent. This will prevent
        // further messages to be sent, but it remains possible to drain the external channel
        // buffer.
        match this.on_terminate.poll(cx) {
            Poll::Pending => {}
            Poll::Ready(_) => {
                this.external.close();
            }
        }

        // Depending on the turn, pick the stream to be polled first.
        let ((first_is_external, first), (second_is_external, second)) = match this.turn {
            Turn::External => ((true, this.external), (false, this.internal)),
            Turn::Internal => ((false, this.internal), (true, this.external)),
        };

        // Toggle the turn so we poll the other stream in the next poll_next call.
        this.turn.advance();

        // Poll the first stream and track whether it's drained or not.
        let first_drained = match first.poll_next(cx) {
            Poll::Pending => false,
            Poll::Ready(None) => {
                // If this stream is the external one, notify once that it has been drained.
                if first_is_external {
                    if let Some(snd) = this.on_external_drained.take() {
                        snd.send(()).unwrap_or_else(|err| {
                            error!(?err, "Failed to notify the external events have been drained.");
                        });
                    };
                }
                true
            }
            res @ Poll::Ready(Some(_)) => return res,
        };

        match second.poll_next(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(None) => {
                // If this stream is the external one, notify once that it has been drained.
                if second_is_external {
                    if let Some(snd) = this.on_external_drained.take() {
                        snd.send(()).unwrap_or_else(|err| {
                            error!(?err, "Failed to notify the external events have been drained.");
                        });
                    };
                }

                // If the first stream was also drained, then we are done. Otherwise, this stream
                // remains pending.
                if first_drained {
                    Poll::Ready(None)
                } else {
                    Poll::Pending
                }
            }
            res @ Poll::Ready(Some(_)) => {
                // If the first stream wasn't drained, then make sure we continue with that other
                // stream in the next call to poll_next as we just had an item to return from this
                // second stream. Therefore, we undo the toggling of the turn done initially.
                if !first_drained {
                    this.turn.advance();
                }
                res
            }
        }
    }
}

/// Allows to termiante external event ingestion.
pub struct TerminateHandle {
    snd: oneshot::Sender<()>,
    external_drained: oneshot::Receiver<()>,
}

impl TerminateHandle {
    /// Terminates external event ingestion. Buffered events will be drained. The returned future
    /// will complete once all buffered external events have been drained.
    pub async fn terminate(self) {
        self.snd.send(()).unwrap_or_else(|err| {
            error!(?err, "Failed to terminate the external event ingestion.");
        });
        self.external_drained
            .await
            .unwrap_or_else(|err| error!(?err, "Error waiting for external events to be drained."));
    }
}

/// Allows to emit events of a restricted set of types.
///
/// Event producers will receive a `Dispatcher` instance that will allow them to emit events of
/// restricted set of types.
pub struct Dispatcher {
    allowed_events: BTreeSet<AnyEventType>,
    sender: Option<mpsc::Sender<Event>>,
}

/// Returns a no-op dispatcher.
impl Default for Dispatcher {
    fn default() -> Self {
        Self { allowed_events: BTreeSet::new(), sender: None }
    }
}

impl Dispatcher {
    fn new(allowed_events: BTreeSet<AnyEventType>, sender: mpsc::Sender<Event>) -> Self {
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
    pub fn new_for_test(allowed_events: BTreeSet<AnyEventType>) -> (mpsc::Receiver<Event>, Self) {
        let (sender, receiver) = mpsc::channel(100);
        (receiver, Self::new(allowed_events, sender))
    }
}

struct EventStreamLogger {
    counters: BTreeMap<AnyEventType, inspect::UintProperty>,
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
            EventPayload::ComponentStopped(ComponentStoppedPayload { component })
            | EventPayload::DiagnosticsReady(DiagnosticsReadyPayload { component, .. })
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
    MissingConsumer(AnyEventType),

    #[error("Missing producer for event type {0:?}")]
    MissingProducer(AnyEventType),
}

/// Configuration for an event producer.
pub struct ProducerConfig<'a, T> {
    /// The event producer that will receive a `Dispatcher`
    pub producer: &'a mut T,

    /// The set of events that the `producer` will be allowed to emit.
    pub events: Vec<EventType>,

    /// The set of singleton events that the `producer` will be allowed to emit.
    pub singleton_events: Vec<SingletonEventType>,

    /// The type of the producer.
    pub producer_type: ProducerType,
}

/// Definition of the type of producers.
pub enum ProducerType {
    /// An external producer emits events originating externally and that the archivist ingests.
    /// These producers can be stopped to ensure all of their events are drained and handled when
    /// shutting down the archivist.
    External,

    /// An internal producer emits events that are generated internally in the archivist.
    /// These producers cannot be stopped and there's no guarantee their messages will be
    /// drained and handled when shutting down the archivist.
    Internal,
}

/// Configuration for an event consumer.
pub struct ConsumerConfig<'a, T> {
    /// The event consumer that will receive events when they are emitted by producers.
    pub consumer: &'a Arc<T>,

    /// The set of event types that the `consumer` will receive.
    pub events: Vec<EventType>,

    /// The set of singleton event types that the `consumer` will receive.
    pub singleton_events: Vec<SingletonEventType>,
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
        async fn emit(&mut self, event_type: AnyEventType, identity: ComponentIdentity) {
            let event = match event_type {
                AnyEventType::General(EventType::ComponentStopped) => Event {
                    timestamp: zx::Time::from_nanos(FAKE_TIMESTAMP),
                    payload: EventPayload::ComponentStopped(ComponentStoppedPayload {
                        component: identity,
                    }),
                },
                AnyEventType::Singleton(SingletonEventType::DiagnosticsReady) => Event {
                    timestamp: zx::Time::from_nanos(FAKE_TIMESTAMP),
                    payload: EventPayload::DiagnosticsReady(DiagnosticsReadyPayload {
                        component: identity,
                        directory: None,
                    }),
                },
                AnyEventType::Singleton(SingletonEventType::LogSinkRequested) => Event {
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
            producer_type: ProducerType::Internal,
            events: vec![],
            singleton_events: vec![SingletonEventType::DiagnosticsReady],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![SingletonEventType::DiagnosticsReady],
        });

        // An explicit match is needed here since unwrap_err requires Debug implemented for both T
        // and E in Result<T, E> and T is a pair which second element is `impl Future` which
        // doesn't implement Debug.
        match router.start(RouterOptions::default()) {
            Err(err) => {
                assert_matches!(
                    err,
                    RouterError::MissingProducer(AnyEventType::General(
                        EventType::ComponentStopped
                    ))
                );
            }
            Ok(_) => panic!("expected an error from routing events"),
        }

        let mut producer = TestEventProducer::default();
        let (_receiver, consumer) = TestEventConsumer::new();
        let mut router = EventRouter::new(inspect::Node::default());
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            producer_type: ProducerType::External,
            events: vec![],
            singleton_events: vec![SingletonEventType::DiagnosticsReady],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![],
            singleton_events: vec![SingletonEventType::LogSinkRequested],
        });

        match router.start(RouterOptions::default()) {
            Err(err) => {
                assert_matches!(
                    err,
                    RouterError::MissingConsumer(AnyEventType::Singleton(
                        SingletonEventType::DiagnosticsReady
                    )) | RouterError::MissingProducer(AnyEventType::Singleton(
                        SingletonEventType::LogSinkRequested,
                    ))
                );
            }
            Ok(_) => panic!("expected an error from routing events"),
        }
    }

    #[fuchsia::test]
    async fn singleton_event_subscription() {
        let mut producer = TestEventProducer::default();
        let (mut first_receiver, first_consumer) = TestEventConsumer::new();
        let (mut second_receiver, second_consumer) = TestEventConsumer::new();
        let mut router = EventRouter::new(inspect::Node::default());
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            producer_type: ProducerType::External,
            events: vec![],
            singleton_events: vec![SingletonEventType::LogSinkRequested],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &first_consumer,
            events: vec![],
            singleton_events: vec![SingletonEventType::LogSinkRequested],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &second_consumer,
            events: vec![],
            singleton_events: vec![SingletonEventType::LogSinkRequested],
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
    async fn regular_event_subscription() {
        let mut producer = TestEventProducer::default();
        let (mut first_receiver, first_consumer) = TestEventConsumer::new();
        let (mut second_receiver, second_consumer) = TestEventConsumer::new();
        let inspector = inspect::Inspector::new();
        let mut router = EventRouter::new(inspector.root().create_child("events"));
        router.add_producer(ProducerConfig {
            producer: &mut producer,
            producer_type: ProducerType::External,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &first_consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &second_consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });

        let (_terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);
        let timestamp = zx::Time::get_monotonic();

        // Emit an event
        producer
            .dispatcher
            .emit(Event {
                timestamp,
                payload: EventPayload::ComponentStopped(ComponentStoppedPayload {
                    component: IDENTITY.clone(),
                }),
            })
            .await
            .unwrap();

        // Both consumers receive the exact same event.
        let first_event = first_receiver.next().await.unwrap();
        assert_matches!(first_event, Event {
            payload: EventPayload::ComponentStopped(payload),
            ..
        } => {
            assert_eq!(payload, ComponentStoppedPayload { component: IDENTITY.clone() });
        });
        let second_event = second_receiver.next().await.unwrap();
        assert_matches!(
            second_event,
            Event { timestamp: t, payload: EventPayload::ComponentStopped(payload) } => {
                assert_eq!(payload, ComponentStoppedPayload { component: IDENTITY.clone() });
                assert_eq!(timestamp, t);
            }
        );
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
            producer_type: ProducerType::Internal,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &first_consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &second_consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_consumer(ConsumerConfig {
            consumer: &third_consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
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
                payload: EventPayload::ComponentStopped(ComponentStoppedPayload {
                    component: IDENTITY.clone(),
                }),
            })
            .await
            .unwrap();

        // We see the event only in the receiver which consumer wasn't dropped.
        let event = second_receiver.next().await.unwrap();
        assert_matches!(event.payload, EventPayload::ComponentStopped(_));
        assert!(first_receiver.next().now_or_never().unwrap().is_none());
        assert!(third_receiver.next().now_or_never().unwrap().is_none());

        // We see additional events in the second receiver which remains alive.
        producer
            .dispatcher
            .emit(Event {
                timestamp: zx::Time::get_monotonic(),
                payload: EventPayload::ComponentStopped(ComponentStoppedPayload {
                    component: IDENTITY.clone(),
                }),
            })
            .await
            .unwrap();
        let event = second_receiver.next().await.unwrap();
        assert_matches!(event.payload, EventPayload::ComponentStopped(_));
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
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![
                SingletonEventType::LogSinkRequested,
                SingletonEventType::DiagnosticsReady,
            ],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer1,
            producer_type: ProducerType::Internal,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![SingletonEventType::DiagnosticsReady],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer2,
            producer_type: ProducerType::Internal,
            events: vec![],
            singleton_events: vec![SingletonEventType::LogSinkRequested],
        });

        producer1
            .emit(
                AnyEventType::Singleton(SingletonEventType::DiagnosticsReady),
                LEGACY_IDENTITY.clone(),
            )
            .await;
        producer1
            .emit(AnyEventType::General(EventType::ComponentStopped), LEGACY_IDENTITY.clone())
            .await;

        producer2
            .emit(AnyEventType::Singleton(SingletonEventType::LogSinkRequested), IDENTITY.clone())
            .await;

        // Consume the events.
        let (_terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);
        receiver.take(3).collect::<Vec<_>>().await;

        assert_data_tree!(inspector, root: {
            events: {
                event_counts: {
                    component_stopped: 1u64,
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
                        event: "component_stopped",
                        moniker: "a/b/foo.cmx:12345"
                    },
                    "2": {
                        "@time": inspect::testing::AnyProperty,
                        event: "log_sink_requested",
                        moniker: "a/b"
                    },
                }
            }
        });
    }

    #[fuchsia::test]
    async fn event_stream_round_robin_semantics() {
        let inspector = inspect::Inspector::new();
        let mut router = EventRouter::new(inspector.root().create_child("events"));
        let mut producer1 = TestEventProducer::default();
        let mut producer2 = TestEventProducer::default();
        let (receiver, consumer) = TestEventConsumer::new();
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer1,
            producer_type: ProducerType::Internal,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_producer(ProducerConfig {
            producer: &mut producer2,
            producer_type: ProducerType::External,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });

        let identity = |moniker| {
            ComponentIdentity::from_identifier_and_url(
                ComponentIdentifier::parse_from_moniker(moniker).unwrap(),
                TEST_URL,
            )
        };

        producer1.emit(AnyEventType::General(EventType::ComponentStopped), identity("./b")).await;
        producer1.emit(AnyEventType::General(EventType::ComponentStopped), identity("./d")).await;
        producer2.emit(AnyEventType::General(EventType::ComponentStopped), identity("./a")).await;
        producer2.emit(AnyEventType::General(EventType::ComponentStopped), identity("./c")).await;

        // We should see an event from each producer followed by an event from the other producer.
        // Also events from each producer must be in order.
        let (_terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);
        let events = receiver.take(4).collect::<Vec<_>>().await;

        let expected_events = vec![
            stopped(identity("./a")),
            stopped(identity("./b")),
            stopped(identity("./c")),
            stopped(identity("./d")),
        ];
        assert_eq!(events.len(), expected_events.len());
        for (event, expected_event) in std::iter::zip(events, expected_events) {
            assert_event(event, expected_event);
        }
    }

    #[fuchsia::test]
    async fn external_stream_draining() {
        let inspector = inspect::Inspector::new();
        let mut router = EventRouter::new(inspector.root().create_child("events"));
        let mut internal_producer = TestEventProducer::default();
        let mut external_producer = TestEventProducer::default();
        let (mut receiver, consumer) = TestEventConsumer::new();
        router.add_consumer(ConsumerConfig {
            consumer: &consumer,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_producer(ProducerConfig {
            producer: &mut internal_producer,
            producer_type: ProducerType::Internal,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });
        router.add_producer(ProducerConfig {
            producer: &mut external_producer,
            producer_type: ProducerType::External,
            events: vec![EventType::ComponentStopped],
            singleton_events: vec![],
        });

        internal_producer
            .emit(AnyEventType::General(EventType::ComponentStopped), LEGACY_IDENTITY.clone())
            .await;
        external_producer
            .emit(AnyEventType::General(EventType::ComponentStopped), IDENTITY.clone())
            .await;

        let (terminate_handle, fut) = router.start(RouterOptions::default()).unwrap();
        let _router_task = fasync::Task::spawn(fut);
        let on_drained = terminate_handle.terminate();
        let drain_finished = fasync::Task::spawn(async move { on_drained.await });

        assert_event(receiver.next().await.unwrap(), stopped(IDENTITY.clone()));
        assert_event(receiver.next().await.unwrap(), stopped(LEGACY_IDENTITY.clone()));

        // This future must be complete now.
        drain_finished.await;

        // We must never see any new event emitted by the external producer. But we must see
        // events emitted by the internal producer.
        external_producer
            .emit(AnyEventType::General(EventType::ComponentStopped), IDENTITY.clone())
            .await;
        assert!(receiver.next().now_or_never().is_none());
        internal_producer
            .emit(AnyEventType::General(EventType::ComponentStopped), LEGACY_IDENTITY.clone())
            .await;
        assert_event(receiver.next().await.unwrap(), stopped(LEGACY_IDENTITY.clone()));
    }

    fn assert_event(event: Event, other: Event) {
        assert_eq!(event.timestamp, other.timestamp);
        match (event.payload, other.payload) {
            (
                EventPayload::ComponentStopped(payload),
                EventPayload::ComponentStopped(other_payload),
            ) => {
                assert_eq!(payload, other_payload);
            }
            _ => unimplemented!("no other combinations are expected in these tests"),
        }
    }

    fn stopped(identity: ComponentIdentity) -> Event {
        Event {
            timestamp: zx::Time::from_nanos(FAKE_TIMESTAMP),
            payload: EventPayload::ComponentStopped(ComponentStoppedPayload {
                component: identity,
            }),
        }
    }
}
