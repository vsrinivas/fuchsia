// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    container::ComponentIdentity,
    logs::{
        budget::BudgetHandle,
        buffer::{ArcList, LazyItem},
        error::StreamError,
        message::MessageWithStats,
        multiplex::PinStream,
        socket::{Encoding, LogMessageSocket},
        stats::LogStreamStats,
    },
};
use fidl::endpoints::RequestStream;
use fidl_fuchsia_diagnostics::{Interest, StreamMode};
use fidl_fuchsia_logger::{
    LogInterestSelector, LogSinkControlHandle, LogSinkRequest, LogSinkRequestStream,
};
use fuchsia_async::Task;
use futures::{channel::mpsc, prelude::*};
use parking_lot::Mutex;
use std::sync::Arc;
use tracing::{debug, error, warn};

pub struct LogsArtifactsContainer {
    /// The source of logs in this container.
    pub identity: Arc<ComponentIdentity>,

    /// Inspect instrumentation.
    pub stats: Arc<LogStreamStats>,

    /// Our handle to the budget manager, used to request space in the overall budget before storing
    /// messages in our cache.
    budget: BudgetHandle,

    /// Buffer for all log messages.
    buffer: ArcList<MessageWithStats>,

    /// Mutable state for the container.
    state: Mutex<ContainerState>,
}

struct ContainerState {
    /// Whether we think the component is currently running.
    is_live: bool,

    /// Number of sockets currently being drained for this component.
    num_active_sockets: u64,

    /// Number of LogSink channels currently being listened to for this component.
    num_active_channels: u64,

    /// Current interest for this component.
    interest: Interest,

    /// Control handles for connected clients.
    control_handles: Vec<LogSinkControlHandle>,
}

impl LogsArtifactsContainer {
    pub fn new(
        identity: Arc<ComponentIdentity>,
        interest_selectors: &[LogInterestSelector],
        stats: LogStreamStats,
        budget: BudgetHandle,
    ) -> Self {
        let new = Self {
            identity,
            budget,
            buffer: Default::default(),
            state: Mutex::new(ContainerState {
                is_live: true,
                num_active_channels: 0,
                num_active_sockets: 0,
                control_handles: vec![],
                interest: Interest::EMPTY,
            }),
            stats: Arc::new(stats),
        };

        // there are no control handles so this won't notify anyone
        new.update_interest(interest_selectors);

        new
    }

    /// Returns a stream of this component's log messages.
    ///
    /// # Dropped logs
    ///
    /// When messages are evicted from our internal buffers before a client can read them, they
    /// are surfaced here as an `LazyItem::ItemsDropped` variant. We report these as synthesized
    /// messages with the timestamp populated as the most recent timestamp from the stream.
    pub fn cursor(&self, mode: StreamMode) -> PinStream<Arc<MessageWithStats>> {
        let identity = self.identity.clone();
        let earliest_timestamp =
            self.buffer.peek_front().map(|f| *f.metadata.timestamp as i64).unwrap_or(0);
        Box::pin(self.buffer.cursor(mode).scan(earliest_timestamp, move |last_timestamp, item| {
            futures::future::ready(Some(match item {
                LazyItem::Next(m) => {
                    *last_timestamp = m.metadata.timestamp.into();
                    m
                }
                LazyItem::ItemsDropped(n) => Arc::new(MessageWithStats::for_dropped(
                    n,
                    identity.as_ref().into(),
                    *last_timestamp,
                )),
            }))
        }))
    }

    /// Handle `LogSink` protocol on `stream`. Each socket received from the `LogSink` client is
    /// drained by a `Task` which is sent on `sender`. The `Task`s do not complete until their
    /// sockets have been closed.
    ///
    /// Sends an `OnRegisterInterest` message right away so producers know someone is listening.
    /// We send `Interest::EMPTY` unless a different interest has previously been specified for
    /// this component.
    pub fn handle_log_sink(
        self: &Arc<Self>,
        stream: LogSinkRequestStream,
        sender: mpsc::UnboundedSender<Task<()>>,
    ) {
        self.state.lock().num_active_channels += 1;
        let task = Task::spawn(self.clone().actually_handle_log_sink(stream, sender.clone()));
        sender.unbounded_send(task).expect("channel is live for whole program");
    }

    /// This function does not return until the channel is closed.
    async fn actually_handle_log_sink(
        self: Arc<Self>,
        mut stream: LogSinkRequestStream,
        sender: mpsc::UnboundedSender<Task<()>>,
    ) {
        debug!(%self.identity, "Draining LogSink channel.");
        {
            let control = stream.control_handle();
            let mut state = self.state.lock();
            control.send_on_register_interest(state.interest.clone()).ok();
            state.control_handles.push(control);
        }

        macro_rules! handle_socket {
            ($ctor:ident($socket:ident, $control_handle:ident)) => {{
                match LogMessageSocket::$ctor($socket, self.identity.clone(), self.stats.clone()) {
                    Ok(log_stream) => {
                        self.state.lock().num_active_sockets += 1;
                        let task = Task::spawn(self.clone().drain_messages(log_stream));
                        sender.unbounded_send(task).expect("channel alive for whole program");
                    }
                    Err(e) => {
                        $control_handle.shutdown();
                        warn!(?self.identity, %e, "error creating socket")
                    }
                };
            }}
        }

        while let Some(next) = stream.next().await {
            match next {
                Ok(LogSinkRequest::Connect { socket, control_handle }) => {
                    handle_socket! {new(socket, control_handle)};
                }
                Ok(LogSinkRequest::ConnectStructured { socket, control_handle }) => {
                    handle_socket! {new_structured(socket, control_handle)};
                }
                Err(e) => error!(%self.identity, %e, "error handling log sink"),
            }
        }
        debug!(%self.identity, "LogSink channel closed.");
        self.state.lock().num_active_channels -= 1;
    }

    /// Drain a `LogMessageSocket` which wraps a socket from a component
    /// generating logs.
    pub async fn drain_messages<E>(self: Arc<Self>, mut log_stream: LogMessageSocket<E>)
    where
        E: Encoding + Unpin,
    {
        debug!(%self.identity, "Draining messages from a socket.");
        loop {
            match log_stream.next().await {
                Ok(message) => {
                    self.ingest_message(message);
                }
                Err(StreamError::Closed) => break,
                Err(e) => {
                    warn!(source = %self.identity, %e, "closing socket");
                    break;
                }
            }
        }
        debug!(%self.identity, "Socket closed.");
        self.state.lock().num_active_sockets -= 1;
    }

    /// Updates log stats in inspect and push the message onto the container's buffer.
    pub fn ingest_message(&self, message: MessageWithStats) {
        self.budget.allocate(message.metadata.size_bytes);
        self.stats.ingest_message(&message);
        self.buffer.push_back(message);
    }

    /// Set the `Interest` for this component, calling `LogSink/OnRegisterInterest` with all
    /// control handles if it is a change from the previous interest.
    pub fn update_interest(&self, interest_selectors: &[LogInterestSelector]) {
        let mut new_interest = Interest::EMPTY;
        for selector in interest_selectors {
            if selectors::match_moniker_against_component_selector(
                &self.identity.relative_moniker,
                &selector.selector,
            )
            .unwrap_or_default()
            {
                new_interest = selector.interest.clone();
            }
        }

        let mut state = self.state.lock();
        if state.interest != new_interest {
            debug!(%self.identity, ?new_interest, "Updating interest.");
            state
                .control_handles
                .retain(|handle| handle.send_on_register_interest(new_interest.clone()).is_ok());
            state.interest = new_interest;
        }
    }

    pub fn mark_started(&self) {
        self.state.lock().is_live = true;
    }

    pub fn mark_stopped(&self) {
        self.state.lock().is_live = false;
    }

    /// Remove the oldest message from this buffer, returning it.
    pub fn pop(&self) -> Option<Arc<MessageWithStats>> {
        self.buffer.pop_front()
    }

    /// Returns `true` if this container corresponds to a running component, still has log messages
    /// or still has pending objects to drain.
    pub fn should_retain(&self) -> bool {
        let state = self.state.lock();
        state.is_live
            || state.num_active_sockets > 0
            || state.num_active_channels > 0
            || !self.buffer.is_empty()
    }

    /// Returns the timestamp of the earliest log message in this container's buffer, if any.
    pub fn oldest_timestamp(&self) -> Option<i64> {
        self.buffer.peek_front().map(|m| m.metadata.timestamp.into())
    }

    /// Stop accepting new messages, ensuring that pending Cursors return Poll::Ready(None) after
    /// consuming any messages received before this call.
    pub fn terminate(&self) {
        self.buffer.terminate();
    }

    #[cfg(test)]
    pub fn buffer(&self) -> &ArcList<MessageWithStats> {
        &self.buffer
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        events::types::{ComponentIdentifier, MonikerSegment},
        logs::budget::BudgetManager,
    };
    use fidl_fuchsia_diagnostics::{ComponentSelector, Severity, StringSelector};
    use fidl_fuchsia_logger::LogSinkMarker;
    use matches::assert_matches;

    #[fuchsia::test]
    async fn update_interest() {
        let moniker_segment_1 = "foo".to_string();
        let moniker_segment_2 = "bar".to_string();
        // Initialize container
        let budget_manager = BudgetManager::new(0);
        let container = Arc::new(LogsArtifactsContainer::new(
            Arc::new(ComponentIdentity::from_identifier_and_url(
                &ComponentIdentifier::Moniker(vec![
                    MonikerSegment {
                        name: moniker_segment_1.clone(),
                        collection: None,
                        instance_id: "0".to_string(),
                    },
                    MonikerSegment {
                        name: moniker_segment_2.clone(),
                        collection: None,
                        instance_id: "0".to_string(),
                    },
                ]),
                "fuchsia-pkg://test",
            )),
            &[],
            LogStreamStats::default(),
            budget_manager.handle(),
        ));
        let container_for_task = container.clone();

        // Connect out LogSink under test and take its events channel.
        let (sender, _recv) = mpsc::unbounded();
        let (log_sink, stream) =
            fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().expect("create log sink");
        container_for_task.handle_log_sink(stream, sender);

        // Verify we get the initial empty interest.
        let mut event_stream = log_sink.take_event_stream();
        assert_eq!(
            event_stream.next().await.unwrap().unwrap().into_on_register_interest().unwrap(),
            Interest::EMPTY,
        );

        // We shouldn't see this interest update since it doesn't match the
        // moniker.
        container.update_interest(&[LogInterestSelector {
            selector: ComponentSelector {
                moniker_segments: Some(vec![StringSelector::ExactMatch("foo".to_string())]),
                ..ComponentSelector::EMPTY
            },
            interest: Interest { min_severity: Some(Severity::Info), ..Interest::EMPTY },
        }]);

        assert_matches!(event_stream.next().now_or_never(), None);

        // We should see this interest update.
        container.update_interest(&[LogInterestSelector {
            selector: ComponentSelector {
                moniker_segments: Some(vec![
                    StringSelector::ExactMatch(moniker_segment_1),
                    StringSelector::ExactMatch(moniker_segment_2),
                ]),
                ..ComponentSelector::EMPTY
            },
            interest: Interest { min_severity: Some(Severity::Info), ..Interest::EMPTY },
        }]);

        // Verify we see the last interest we set.
        assert_eq!(
            event_stream
                .next()
                .await
                .unwrap()
                .unwrap()
                .into_on_register_interest()
                .unwrap()
                .min_severity,
            Some(Severity::Info),
        );
    }
}
