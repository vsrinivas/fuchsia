// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        container::ComponentIdentity,
        events::{error::EventError, types::*},
    },
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode},
    futures::{
        channel::{mpsc, oneshot},
        Future, Stream, StreamExt,
    },
    parking_lot::Mutex,
    pin_project::pin_project,
    std::{
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
};

/// Tracks all event sources and listens to events coming from them pushing them into an MPSC
/// channel.
pub struct EventSourceRegistry {
    /// The registered event sources
    sources: Arc<EventSourceStore>,

    /// The sender end of th events MPSC channel. A clone of this is given to every event source
    /// that is added when starting to listen for events on them.
    event_sender: mpsc::Sender<ComponentEvent>,

    /// The root node for events instrumentation.
    node: inspect::Node,

    /// The stream that is exposed to clients. It can only be taken once.
    event_stream: Option<EventStream>,

    /// Used to close the receiving end of `event_sender` to stop receiving new events in the
    /// channel and proceed to drain existing ones.
    stop_accepting_events: Option<oneshot::Sender<()>>,
}

const RECENT_EVENT_LIMIT: usize = 200;

impl EventSourceRegistry {
    /// Creates a new event listener.
    pub fn new(node: inspect::Node) -> Self {
        let sources_node = node.create_child("sources");
        let (event_sender, receiver) = mpsc::channel(CHANNEL_CAPACITY);
        let sources =
            Arc::new(EventSourceStore { sources: Mutex::new(Vec::new()), node: sources_node });
        let (stop_accepting_events, event_stream) = EventStream::new(receiver);

        let registry = Self {
            sources,
            event_stream: Some(event_stream),
            node,
            event_sender,
            stop_accepting_events: Some(stop_accepting_events),
        };
        registry
    }

    /// Adds an event source and starts listening for events on it.
    pub async fn add_source(&mut self, name: impl ToString, source: Box<dyn EventSource>) {
        self.sources
            .add(
                EventSourceRegistration { name: name.to_string(), source },
                self.event_sender.clone(),
            )
            .await;
    }

    /// Takes the single stream where component events are pushed.
    pub async fn take_stream(&mut self) -> Result<ComponentEventStream, EventError> {
        match self.event_stream.take() {
            None => Err(EventError::StreamAlreadyTaken),
            Some(stream) => {
                let mut logger = EventStreamLogger::new(&self.node);
                Ok(Box::pin(stream.map(move |event| {
                    logger.log_event(&event);
                    event
                })))
            }
        }
    }

    pub fn terminate(&mut self) {
        if let Some(sender) = self.stop_accepting_events.take() {
            // Swallow error, if the stream was dropped, it's already terminated.
            let _ = sender.send(());
        }
    }
}

pub struct EventSourceRegistration {
    pub name: String,
    pub source: Box<dyn EventSource>,
}

struct EventSourceStore {
    sources: Mutex<Vec<Box<dyn EventSource>>>,

    /// Child of `node`. Holds the status of every event source that is added.
    node: inspect::Node,
}

impl EventSourceStore {
    async fn add(
        &self,
        mut registration: EventSourceRegistration,
        event_sender: mpsc::Sender<ComponentEvent>,
    ) {
        let source_node = self.node.create_child(registration.name);
        match registration.source.listen(event_sender).await {
            Ok(()) => {
                source_node.record_string("status", "ok");
            }
            Err(err) => {
                source_node.record_string("status", format!("error: {:?}", err));
            }
        }
        self.sources.lock().push(registration.source);
        self.node.record(source_node);
    }
}

#[pin_project]
pub struct EventStream {
    #[pin]
    receiver: mpsc::Receiver<ComponentEvent>,
    #[pin]
    terminate_rx: oneshot::Receiver<()>,
    receiver_active: bool,
}

impl EventStream {
    fn new(receiver: mpsc::Receiver<ComponentEvent>) -> (oneshot::Sender<()>, Self) {
        let (terminate_sender, terminate_rx) = oneshot::channel();
        (terminate_sender, Self { receiver, terminate_rx, receiver_active: true })
    }
}

impl Stream for EventStream {
    type Item = ComponentEvent;
    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let mut this = self.project();
        if *this.receiver_active {
            if let Poll::Ready(_) = this.terminate_rx.poll(cx) {
                // Close the receiver, but still allow to drain all its messages.
                this.receiver.close();
                *this.receiver_active = false;
            }
        }
        this.receiver.poll_next(cx)
    }
}

struct EventStreamLogger {
    components_started: inspect::UintProperty,
    components_stopped: inspect::UintProperty,
    components_seen_running: inspect::UintProperty,
    diagnostics_directories_seen: inspect::UintProperty,
    log_sink_requests_seen: inspect::UintProperty,
    component_log_node: BoundedListNode,
}

impl EventStreamLogger {
    /// Creates a new event logger. All inspect data will be written as children of `parent`.
    pub fn new(parent: &inspect::Node) -> Self {
        let components_started = parent.create_uint("components_started", 0);
        let components_seen_running = parent.create_uint("components_seen_running", 0);
        let components_stopped = parent.create_uint("components_stopped", 0);
        let diagnostics_directories_seen = parent.create_uint("diagnostics_directories_seen", 0);
        let log_sink_requests_seen = parent.create_uint("log_sink_requests_seen", 0);
        let component_log_node =
            BoundedListNode::new(parent.create_child("recent_events"), RECENT_EVENT_LIMIT);
        Self {
            components_started,
            components_stopped,
            components_seen_running,
            diagnostics_directories_seen,
            log_sink_requests_seen,
            component_log_node,
        }
    }

    /// Log a new component event to inspect.
    pub fn log_event(&mut self, event: &ComponentEvent) {
        match event {
            ComponentEvent::Start(start) => {
                self.components_started.add(1);
                self.log_inspect("START", &start.metadata.identity);
            }
            ComponentEvent::Stop(stop) => {
                self.components_stopped.add(1);
                self.log_inspect("STOP", &stop.metadata.identity);
            }
            ComponentEvent::Running(running) => {
                self.components_seen_running.add(1);
                self.log_inspect("RUNNING", &running.metadata.identity);
            }
            ComponentEvent::DiagnosticsReady(diagnostics_ready) => {
                self.diagnostics_directories_seen.add(1);
                self.log_inspect("DIAGNOSTICS_DIR_READY", &diagnostics_ready.metadata.identity);
            }
            ComponentEvent::LogSinkRequested(log_sink_requested) => {
                self.log_sink_requests_seen.add(1);
                self.log_inspect("LOG_SINK_REQUESTED", &log_sink_requested.metadata.identity);
            }
        }
    }

    fn log_inspect(&mut self, event_name: &str, identity: &ComponentIdentity) {
        inspect_log!(self.component_log_node,
            event: event_name,
            moniker: identity.rendered_moniker,
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_trait::async_trait,
        fidl_fuchsia_logger::LogSinkMarker,
        fuchsia_inspect::{self as inspect, assert_data_tree},
        fuchsia_zircon as zx,
        futures::SinkExt,
        lazy_static::lazy_static,
    };

    struct FakeEventSource {}
    struct FakeLegacyProvider {}
    struct FakeFutureProvider {}

    lazy_static! {
        static ref TEST_URL: String = "NO-OP URL".to_string();
        static ref LEGACY_ID: ComponentIdentifier = ComponentIdentifier::Legacy {
            instance_id: "12345".to_string(),
            moniker: vec!["a", "b", "foo.cmx"].into(),
        };
        static ref LEGACY_IDENTITY: ComponentIdentity =
            ComponentIdentity::from_identifier_and_url(&*LEGACY_ID, &*TEST_URL);
        static ref MONIKER_ID: ComponentIdentifier =
            ComponentIdentifier::parse_from_moniker("./a:0/b:1").unwrap();
        static ref MONIKER_IDENTITY: ComponentIdentity =
            ComponentIdentity::from_identifier_and_url(&*MONIKER_ID, &*TEST_URL);
    }

    #[async_trait]
    impl EventSource for FakeEventSource {
        async fn listen(
            &mut self,
            mut sender: mpsc::Sender<ComponentEvent>,
        ) -> Result<(), EventError> {
            let shared_data = EventMetadata {
                identity: ComponentIdentity::from_identifier_and_url(&*MONIKER_ID, &*TEST_URL),
                timestamp: zx::Time::get_monotonic(),
            };

            sender
                .send(ComponentEvent::Start(StartEvent { metadata: shared_data.clone() }))
                .await
                .expect("send start");
            sender
                .send(ComponentEvent::DiagnosticsReady(DiagnosticsReadyEvent {
                    metadata: shared_data.clone(),
                    directory: None,
                }))
                .await
                .expect("send diagnostics ready");
            sender
                .send(ComponentEvent::Stop(StopEvent { metadata: shared_data.clone() }))
                .await
                .expect("send stop");
            let (_, log_sink_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().unwrap();
            sender
                .send(ComponentEvent::LogSinkRequested(LogSinkRequestedEvent {
                    metadata: shared_data.clone(),
                    requests: log_sink_stream,
                }))
                .await
                .expect("send log sink requested");
            Ok(())
        }
    }

    #[async_trait]
    impl EventSource for FakeLegacyProvider {
        async fn listen(
            &mut self,
            mut sender: mpsc::Sender<ComponentEvent>,
        ) -> Result<(), EventError> {
            let shared_data = EventMetadata {
                identity: ComponentIdentity::from_identifier_and_url(&*LEGACY_ID, &*TEST_URL),
                timestamp: zx::Time::get_monotonic(),
            };

            sender
                .send(ComponentEvent::Start(StartEvent { metadata: shared_data.clone() }))
                .await
                .expect("send start");
            sender
                .send(ComponentEvent::DiagnosticsReady(DiagnosticsReadyEvent {
                    metadata: shared_data.clone(),
                    directory: None,
                }))
                .await
                .expect("send diagnostics ready");
            sender
                .send(ComponentEvent::Stop(StopEvent { metadata: shared_data.clone() }))
                .await
                .expect("send stop");
            let (_, log_sink_stream) =
                fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().unwrap();
            sender
                .send(ComponentEvent::LogSinkRequested(LogSinkRequestedEvent {
                    metadata: shared_data.clone(),
                    requests: log_sink_stream,
                }))
                .await
                .expect("send log sink requested");
            Ok(())
        }
    }

    #[async_trait]
    impl EventSource for FakeFutureProvider {
        async fn listen(
            &mut self,
            _sender: mpsc::Sender<ComponentEvent>,
        ) -> Result<(), EventError> {
            Err(EventError::StreamAlreadyTaken)
        }
    }

    async fn validate_events(stream: &mut ComponentEventStream, expected_id: &ComponentIdentity) {
        for i in 0..4 {
            let event = stream.next().await.expect("received event");
            match (i, &event) {
                (0, ComponentEvent::Start(StartEvent { metadata, .. }))
                | (1, ComponentEvent::DiagnosticsReady(DiagnosticsReadyEvent { metadata, .. }))
                | (2, ComponentEvent::Stop(StopEvent { metadata, .. }))
                | (3, ComponentEvent::LogSinkRequested(LogSinkRequestedEvent { metadata, .. })) => {
                    assert_eq!(metadata.identity, *expected_id);
                }
                _ => panic!("unexpected event: {:?}", event),
            }
        }
    }

    #[fuchsia::test]
    async fn joint_event_channel() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("events");
        let mut registry = EventSourceRegistry::new(node);
        registry.add_source("v1", Box::new(FakeLegacyProvider {})).await;
        registry.add_source("v2", Box::new(FakeEventSource {})).await;
        registry.add_source("v3", Box::new(FakeFutureProvider {})).await;
        let mut stream = registry.take_stream().await.expect("take stream succeeds");

        validate_events(&mut stream, &LEGACY_IDENTITY).await;
        validate_events(&mut stream, &MONIKER_IDENTITY).await;

        assert_data_tree!(inspector, root: {
            events: {
                sources: {
                    v1: {
                        status: "ok"
                    },
                    v2: {
                        status: "ok"
                    },
                    v3: {
                        status: "error: StreamAlreadyTaken"
                    }
                },
                components_started: 2u64,
                components_stopped: 2u64,
                components_seen_running: 0u64,
                diagnostics_directories_seen: 2u64,
                log_sink_requests_seen: 2u64,
                recent_events: {
                    "0": {
                        "@time": inspect::testing::AnyProperty,
                        event: "START",
                        moniker: "a/b/foo.cmx:12345"
                    },
                    "1": {
                        "@time": inspect::testing::AnyProperty,
                        event: "DIAGNOSTICS_DIR_READY",
                        moniker: "a/b/foo.cmx:12345"
                    },
                    "2": {
                        "@time": inspect::testing::AnyProperty,
                        event: "STOP",
                        moniker: "a/b/foo.cmx:12345"
                    },
                    "3": {
                        "@time": inspect::testing::AnyProperty,
                        event: "LOG_SINK_REQUESTED",
                        moniker: "a/b/foo.cmx:12345"
                    },
                    "4": {
                        "@time": inspect::testing::AnyProperty,
                        event: "START",
                        moniker: "a/b"
                    },
                    "5": {
                        "@time": inspect::testing::AnyProperty,
                        event: "DIAGNOSTICS_DIR_READY",
                        moniker: "a/b"
                    },
                    "6": {
                        "@time": inspect::testing::AnyProperty,
                        event: "STOP",
                        moniker: "a/b"
                    },
                    "7": {
                        "@time": inspect::testing::AnyProperty,
                        event: "LOG_SINK_REQUESTED",
                        moniker: "a/b"
                    },
                }
            }
        });
    }
}
