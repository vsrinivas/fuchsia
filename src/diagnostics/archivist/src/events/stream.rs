// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::events::types::*,
    fuchsia_inspect::{self as inspect, NumericProperty},
    fuchsia_inspect_contrib::{inspect_log, nodes::BoundedListNode},
    futures::{channel::mpsc, StreamExt},
};

pub struct EventStream {
    sources: Vec<(String, Box<dyn EventSource>)>,

    // Inspect stats
    node: inspect::Node,
    components_started: inspect::UintProperty,
    components_stopped: inspect::UintProperty,
    components_seen_running: inspect::UintProperty,
    diagnostics_directories_seen: inspect::UintProperty,
    component_log_node: BoundedListNode,
}

impl EventStream {
    /// Creates a new event listener.
    pub fn new(node: inspect::Node) -> Self {
        let components_started = node.create_uint("components_started", 0);
        let components_seen_running = node.create_uint("components_seen_running", 0);
        let components_stopped = node.create_uint("components_stopped", 0);
        let diagnostics_directories_seen = node.create_uint("diagnostics_directories_seen", 0);
        let component_log_node = BoundedListNode::new(node.create_child("recent_events"), 50);
        Self {
            sources: Vec::new(),
            node,
            components_started,
            components_stopped,
            components_seen_running,
            diagnostics_directories_seen,
            component_log_node,
        }
    }

    /// Adds an event source to listen from.
    pub fn add_source(&mut self, name: impl Into<String>, source: Box<dyn EventSource>) {
        self.sources.push((name.into(), source));
    }

    /// Subscribe to component lifecycle events.
    /// |node| is the node where stats about events seen will be recorded.
    pub async fn listen(mut self) -> ComponentEventStream {
        let (sender, receiver) = mpsc::channel(CHANNEL_CAPACITY);
        let sources_node = self.node.create_child("sources");
        for (name, source) in &self.sources {
            let source_node = sources_node.create_child(name);
            match source.listen(sender.clone()).await {
                Ok(()) => {}
                Err(err) => source_node.record_string("error", err.to_string()),
            }
            sources_node.record(source_node);
        }
        self.node.record(sources_node);
        Box::pin(receiver.boxed().map(move |event| {
            self.log_event(&event);
            event
        }))
    }

    fn log_event(&mut self, event: &ComponentEvent) {
        match event {
            ComponentEvent::Start(start) => {
                self.components_started.add(1);
                self.log_inspect("START", &start.metadata.component_id);
            }
            ComponentEvent::Stop(stop) => {
                self.components_stopped.add(1);
                self.log_inspect("STOP", &stop.metadata.component_id);
            }
            ComponentEvent::Running(running) => {
                self.components_seen_running.add(1);
                self.log_inspect("RUNNING", &running.metadata.component_id);
            }
            ComponentEvent::DiagnosticsReady(diagnostics_ready) => {
                self.diagnostics_directories_seen.add(1);
                self.log_inspect("DIAGNOSTICS_DIR_READY", &diagnostics_ready.metadata.component_id);
            }
        }
    }

    fn log_inspect(&mut self, event_name: &str, identifier: &ComponentIdentifier) {
        inspect_log!(self.component_log_node,
            event: event_name,
            moniker: identifier.to_string(),
        );
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{format_err, Error},
        async_trait::async_trait,
        fuchsia_async as fasync,
        fuchsia_inspect::{self as inspect, assert_inspect_tree},
        fuchsia_zircon as zx,
        futures::SinkExt,
        lazy_static::lazy_static,
    };

    struct FakeEventSource {}
    struct FakeLegacyProvider {}
    struct FakeFutureProvider {}

    lazy_static! {
        static ref LEGACY_ID: ComponentIdentifier = ComponentIdentifier::Legacy(LegacyIdentifier {
            component_name: "foo.cmx".to_string(),
            instance_id: "12345".to_string(),
            realm_path: RealmPath(vec!["a".to_string(), "b".to_string()]),
        });
        static ref LEGACY_URL: String = "NO-OP URL".to_string();
        static ref MONIKER_ID: ComponentIdentifier =
            ComponentIdentifier::Moniker("a:0/b:1".to_string());
    }

    #[async_trait]
    impl EventSource for FakeEventSource {
        async fn listen(&self, mut sender: mpsc::Sender<ComponentEvent>) -> Result<(), Error> {
            let shared_data = EventMetadata {
                component_id: MONIKER_ID.clone(),
                component_url: LEGACY_URL.clone(),
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
            Ok(())
        }
    }

    #[async_trait]
    impl EventSource for FakeLegacyProvider {
        async fn listen(&self, mut sender: mpsc::Sender<ComponentEvent>) -> Result<(), Error> {
            let shared_data = EventMetadata {
                component_id: LEGACY_ID.clone(),
                component_url: LEGACY_URL.clone(),
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
            Ok(())
        }
    }

    #[async_trait]
    impl EventSource for FakeFutureProvider {
        async fn listen(&self, _sender: mpsc::Sender<ComponentEvent>) -> Result<(), Error> {
            Err(format_err!("not implemented yet"))
        }
    }

    async fn validate_events(stream: &mut ComponentEventStream, expected_id: &ComponentIdentifier) {
        for i in 0..3 {
            let event = stream.next().await.expect("received event");
            match (i, &event) {
                (0, ComponentEvent::Start(StartEvent { metadata, .. }))
                | (1, ComponentEvent::DiagnosticsReady(DiagnosticsReadyEvent { metadata, .. }))
                | (2, ComponentEvent::Stop(StopEvent { metadata, .. })) => {
                    assert_eq!(metadata.component_id, *expected_id);
                }
                _ => panic!("unexpected event: {:?}", event),
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn joint_event_channel() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("events");
        let mut stream = EventStream::new(node);
        stream.add_source("v1", Box::new(FakeLegacyProvider {}));
        stream.add_source("v2", Box::new(FakeEventSource {}));
        stream.add_source("v3", Box::new(FakeFutureProvider {}));
        let mut stream = stream.listen().await;

        validate_events(&mut stream, &LEGACY_ID).await;
        validate_events(&mut stream, &MONIKER_ID).await;

        assert_inspect_tree!(inspector, root: {
            events: {
                sources: {
                    v1: {
                    },
                    v2: {
                    },
                    v3: {
                        error: "not implemented yet"
                    }
                },
                components_started: 2u64,
                components_stopped: 2u64,
                components_seen_running: 0u64,
                diagnostics_directories_seen: 2u64,
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
                        event: "START",
                        moniker: "a:0/b:1"
                    },
                    "4": {
                        "@time": inspect::testing::AnyProperty,
                        event: "DIAGNOSTICS_DIR_READY",
                        moniker: "a:0/b:1"
                    },
                    "5": {
                        "@time": inspect::testing::AnyProperty,
                        event: "STOP",
                        moniker: "a:0/b:1"
                    }
                }
            }
        });
    }
}
