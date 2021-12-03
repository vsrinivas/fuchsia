// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    container::ComponentIdentity,
    logs::{
        budget::BudgetHandle,
        buffer::{ArcList, LazyItem},
        error::StreamError,
        multiplex::PinStream,
        socket::{Encoding, LogMessageSocket},
        stats::LogStreamStats,
        stored_message::StoredMessage,
    },
};
use diagnostics_data::{BuilderArgs, LogsData, LogsDataBuilder};
use fidl::prelude::*;
use fidl_fuchsia_diagnostics::{Interest as FidlInterest, LogInterestSelector, StreamMode};
use fidl_fuchsia_logger::{LogSinkControlHandle, LogSinkRequest, LogSinkRequestStream};
use fuchsia_async::Task;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, prelude::*};
use parking_lot::Mutex;
use std::{cmp::Ordering, collections::BTreeMap, sync::Arc};
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
    buffer: ArcList<StoredMessage>,

    /// Mutable state for the container.
    state: Mutex<ContainerState>,

    /// The time when the container was created by the logging
    /// framework.
    pub event_timestamp: zx::Time,
}

struct ContainerState {
    /// Whether we think the component is currently running.
    is_live: bool,

    /// Number of sockets currently being drained for this component.
    num_active_sockets: u64,

    /// Number of LogSink channels currently being listened to for this component.
    num_active_channels: u64,

    /// Current interest for this component.
    interests: BTreeMap<Interest, usize>,

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
                interests: BTreeMap::new(),
            }),
            stats: Arc::new(stats),
            event_timestamp: zx::Time::get_monotonic(),
        };

        // there are no control handles so this won't notify anyone
        new.update_interest(interest_selectors, &[]);

        new
    }

    /// Returns a stream of this component's log messages.
    ///
    /// # Dropped logs
    ///
    /// When messages are evicted from our internal buffers before a client can read them, they
    /// are surfaced here as an `LazyItem::ItemsRolledOut` variant. We report these as synthesized
    /// messages with the timestamp populated as the most recent timestamp from the stream.
    pub fn cursor(&self, mode: StreamMode) -> PinStream<Arc<LogsData>> {
        let identity = self.identity.clone();
        let earliest_timestamp = self.buffer.peek_front().map(|f| f.timestamp()).unwrap_or(0);
        Box::pin(self.buffer.cursor(mode).scan(earliest_timestamp, move |last_timestamp, item| {
            futures::future::ready(Some(match item {
                LazyItem::Next(m) => {
                    *last_timestamp = m.timestamp();
                    match m.parse(&identity) {
                        Ok(m) => Arc::new(m),
                        Err(err) => {
                            let data = LogsDataBuilder::new(BuilderArgs {
                                moniker: identity.to_string(),
                                timestamp_nanos: (*last_timestamp).into(),
                                component_url: Some(identity.url.clone()),
                                severity: diagnostics_data::Severity::Warn,
                            })
                            .add_error(diagnostics_data::LogError::FailedToParseRecord(format!(
                                "{:?}",
                                err
                            )))
                            .build();
                            Arc::new(data.into())
                        }
                    }
                }
                LazyItem::ItemsRolledOut(n) => {
                    let message = format!("Rolled {} logs out of buffer", n);
                    let data = LogsDataBuilder::new(BuilderArgs {
                        moniker: identity.to_string(),
                        timestamp_nanos: (*last_timestamp).into(),
                        component_url: Some(identity.url.clone()),
                        severity: diagnostics_data::Severity::Warn,
                    })
                    .add_error(diagnostics_data::LogError::RolledOutLogs { count: n })
                    .set_message(message)
                    .build();
                    Arc::new(data)
                }
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
            control.send_on_register_interest(state.min_interest().clone().into()).ok();
            state.control_handles.push(control);
        }

        macro_rules! handle_socket {
            ($ctor:ident($socket:ident, $control_handle:ident)) => {{
                match LogMessageSocket::$ctor($socket, self.stats.clone()) {
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
    pub fn ingest_message(&self, mut message: StoredMessage) {
        self.budget.allocate(message.size());
        self.stats.ingest_message(&message);
        if !message.has_stats() {
            message.with_stats(self.stats.clone());
        }
        self.buffer.push_back(message);
    }

    /// Set the `Interest` for this component, calling `LogSink/OnRegisterInterest` with all
    /// control handles if it is a change from the previous interest. For any match that is also
    /// contained in `previous_selectors`, the previous values will be removed from the set of
    /// interests.
    pub fn update_interest(
        &self,
        interest_selectors: &[LogInterestSelector],
        previous_selectors: &[LogInterestSelector],
    ) {
        let mut new_interest = FidlInterest::EMPTY;
        let mut remove_interest = FidlInterest::EMPTY;
        for selector in interest_selectors {
            if selectors::match_moniker_against_component_selector(
                &self.identity.relative_moniker,
                &selector.selector,
            )
            .unwrap_or_default()
            {
                new_interest = selector.interest.clone();
                // If there are more matches, ignore them, we'll pick the first match.
                break;
            }
        }

        if let Some(previous_selector) = previous_selectors.iter().find(|s| {
            selectors::match_moniker_against_component_selector(
                &self.identity.relative_moniker,
                &s.selector,
            )
            .unwrap_or_default()
        }) {
            remove_interest = previous_selector.interest.clone();
        }

        let mut state = self.state.lock();
        // Unfortunately we cannot use a match statement since `FidlInterest` doesn't derive Eq.
        // It does derive PartialEq though. All these branches will send an interest update if the
        // minimum interest changes after performing the required actions.
        if new_interest == FidlInterest::EMPTY && remove_interest != FidlInterest::EMPTY {
            // Undo the previous interest. There's no new interest to add.
            state.maybe_send_updates(
                |state| {
                    state.erase(&remove_interest);
                },
                &self.identity,
            );
        } else if new_interest != FidlInterest::EMPTY && remove_interest == FidlInterest::EMPTY {
            // Apply the new interest. There's no previous interest to remove.
            state.maybe_send_updates(
                |state| {
                    state.push_interest(new_interest);
                },
                &self.identity,
            );
        } else if new_interest != FidlInterest::EMPTY && remove_interest != FidlInterest::EMPTY {
            // Remove the previous interest and insert the new one.
            state.maybe_send_updates(
                |state| {
                    state.erase(&remove_interest);
                    state.push_interest(new_interest);
                },
                &self.identity,
            );
        }
    }

    /// Resets the `Interest` for this component, calling `LogSink/OnRegisterInterest` with the
    /// lowest interest found in the set of requested interests for all control handles.
    pub fn reset_interest(&self, interest_selectors: &[LogInterestSelector]) {
        for selector in interest_selectors {
            if selectors::match_moniker_against_component_selector(
                &self.identity.relative_moniker,
                &selector.selector,
            )
            .unwrap_or_default()
            {
                let mut state = self.state.lock();
                state.maybe_send_updates(
                    |state| {
                        state.erase(&selector.interest);
                    },
                    &self.identity,
                );
                return;
            }
        }
    }

    pub fn mark_started(&self) {
        self.state.lock().is_live = true;
    }

    pub fn mark_stopped(&self) {
        self.state.lock().is_live = false;
    }

    /// Remove the oldest message from this buffer, returning it.
    pub fn pop(&self) -> Option<Arc<StoredMessage>> {
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
        self.buffer.peek_front().map(|m| m.timestamp())
    }

    /// Stop accepting new messages, ensuring that pending Cursors return Poll::Ready(None) after
    /// consuming any messages received before this call.
    pub fn terminate(&self) {
        self.buffer.terminate();
    }

    #[cfg(test)]
    pub fn buffer(&self) -> &ArcList<StoredMessage> {
        &self.buffer
    }
}

impl ContainerState {
    /// Executes the given callback on the state. If the minimum interest before executing the given
    /// actions and after isn't the same, then the new interest is sent to the registered listeners.
    fn maybe_send_updates<F>(&mut self, action: F, identity: &ComponentIdentity)
    where
        F: FnOnce(&mut ContainerState),
    {
        let prev_min_interest = self.min_interest();
        action(self);
        let new_min_interest = self.min_interest();
        if prev_min_interest == FidlInterest::EMPTY
            || compare_fidl_interest(&new_min_interest, &prev_min_interest) != Ordering::Equal
        {
            debug!(%identity, ?new_min_interest, "Updating interest.");
            self.control_handles.retain(|handle| {
                handle.send_on_register_interest(new_min_interest.clone()).is_ok()
            });
        }
    }

    /// Pushes the given `interest` to the set.
    fn push_interest(&mut self, interest: FidlInterest) {
        if interest != FidlInterest::EMPTY {
            let count = self.interests.entry(interest.into()).or_insert(0);
            *count += 1;
        }
    }

    /// Removes the given `interest` from the set
    fn erase(&mut self, interest: &FidlInterest) {
        let interest = interest.clone().into();
        if let Some(count) = self.interests.get_mut(&interest) {
            if *count <= 1 {
                self.interests.remove(&interest);
            } else {
                *count -= 1;
            }
        }
    }

    /// Returns a copy of the lowest interest in the set. If the set is empty, an EMPTY interest is
    /// returned.
    fn min_interest(&self) -> FidlInterest {
        // btreemap: keys are sorted and ascending.
        self.interests.keys().next().map(|i| i.0.clone()).unwrap_or(FidlInterest::EMPTY)
    }
}

#[derive(Debug, PartialEq)]
struct Interest(FidlInterest);

impl From<FidlInterest> for Interest {
    fn from(interest: FidlInterest) -> Interest {
        Interest(interest)
    }
}

impl std::ops::Deref for Interest {
    type Target = FidlInterest;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl Eq for Interest {}

impl Ord for Interest {
    fn cmp(&self, other: &Self) -> Ordering {
        match (self.min_severity, other.min_severity) {
            (Some(_), None) => Ordering::Greater,
            (None, Some(_)) => Ordering::Less,
            (None, None) => Ordering::Equal,
            (Some(a), Some(b)) => a.cmp(&b),
        }
    }
}

impl PartialOrd for Interest {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(&other))
    }
}

/// Compares the minimum severity of two interests.
fn compare_fidl_interest(a: &FidlInterest, b: &FidlInterest) -> Ordering {
    match (a.min_severity, b.min_severity) {
        (Some(_), None) => Ordering::Greater,
        (None, Some(_)) => Ordering::Less,
        (None, None) => Ordering::Equal,
        (Some(a), Some(b)) => a.cmp(&b),
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
    use fidl_fuchsia_logger::{LogSinkEventStream, LogSinkMarker};
    use matches::assert_matches;

    async fn initialize_container() -> (Arc<LogsArtifactsContainer>, LogSinkEventStream) {
        // Initialize container
        let budget_manager = BudgetManager::new(0);
        let container = Arc::new(LogsArtifactsContainer::new(
            Arc::new(ComponentIdentity::from_identifier_and_url(
                ComponentIdentifier::Moniker(vec![
                    MonikerSegment { name: "foo".to_string(), collection: None },
                    MonikerSegment { name: "bar".to_string(), collection: None },
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
            FidlInterest::EMPTY,
        );

        (container, event_stream)
    }

    #[fuchsia::test]
    async fn update_interest() {
        let (container, mut event_stream) = initialize_container().await;

        // We shouldn't see this interest update since it doesn't match the
        // moniker.
        container.update_interest(&[interest(&["foo"], Some(Severity::Info))], &[]);

        assert_matches!(event_stream.next().now_or_never(), None);

        // We should see this interest update.
        container.update_interest(&[interest(&["foo", "bar"], Some(Severity::Info))], &[]);

        // Verify we see the last interest we set.
        assert_severity(&mut event_stream, Severity::Info).await;
    }

    #[fuchsia::test]
    async fn interest_serverity_semantics() {
        let (container, mut event_stream) = initialize_container().await;

        // Set some interest.
        container.update_interest(&[interest(&["foo", "bar"], Some(Severity::Info))], &[]);
        assert_severity(&mut event_stream, Severity::Info).await;
        assert_matches!(event_stream.next().now_or_never(), None);
        assert_interests(&container, [(Severity::Info, 1)]);

        // Sending a higher interest (WARN > INFO) has no visible effect, even if the new interest
        // (WARN) will be tracked internally until reset.
        container.update_interest(&[interest(&["foo", "bar"], Some(Severity::Warn))], &[]);
        assert_matches!(event_stream.next().now_or_never(), None);
        assert_interests(&container, [(Severity::Info, 1), (Severity::Warn, 1)]);

        // Sending a lower interest (DEBUG < INFO) updates the previous one.
        container.update_interest(&[interest(&["foo", "bar"], Some(Severity::Debug))], &[]);
        assert_severity(&mut event_stream, Severity::Debug).await;
        assert_interests(
            &container,
            [(Severity::Debug, 1), (Severity::Info, 1), (Severity::Warn, 1)],
        );

        // Sending the same interest leads to tracking it twice, but no updates are sent since it's
        // the same minimum interest.
        container.update_interest(&[interest(&["foo", "bar"], Some(Severity::Debug))], &[]);
        assert_matches!(event_stream.next().now_or_never(), None);
        assert_interests(
            &container,
            [(Severity::Debug, 2), (Severity::Info, 1), (Severity::Warn, 1)],
        );

        // The first reset does nothing, since the new minimum interest remains the same (we had
        // inserted twice, therefore we need to reset twice).
        container.reset_interest(&[interest(&["foo", "bar"], Some(Severity::Debug))]);
        assert_matches!(event_stream.next().now_or_never(), None);
        assert_interests(
            &container,
            [(Severity::Debug, 1), (Severity::Info, 1), (Severity::Warn, 1)],
        );

        // The second reset causes a change in minimum interest -> now INFO.
        container.reset_interest(&[interest(&["foo", "bar"], Some(Severity::Debug))]);
        assert_severity(&mut event_stream, Severity::Info).await;
        assert_interests(&container, [(Severity::Info, 1), (Severity::Warn, 1)]);

        // If we pass a previous severity (INFO), then we undo it and set the new one (ERROR).
        // However, we get WARN since that's the minimum severity in the set.
        container.update_interest(
            &[interest(&["foo", "bar"], Some(Severity::Error))],
            &[interest(&["foo", "bar"], Some(Severity::Info))],
        );
        assert_severity(&mut event_stream, Severity::Warn).await;
        assert_interests(&container, [(Severity::Error, 1), (Severity::Warn, 1)]);

        // When we reset warn, now we get ERROR since that's the minimum severity in the set.
        container.reset_interest(&[interest(&["foo", "bar"], Some(Severity::Warn))]);
        assert_severity(&mut event_stream, Severity::Error).await;
        assert_interests(&container, [(Severity::Error, 1)]);

        // When we reset ERROR , we get back to EMPTY since we have removed all interests from the
        // set.
        container.reset_interest(&[interest(&["foo", "bar"], Some(Severity::Error))]);
        assert_eq!(
            event_stream.next().await.unwrap().unwrap().into_on_register_interest().unwrap(),
            FidlInterest::EMPTY,
        );
        assert_interests(&container, []);
    }

    fn interest(moniker: &[&str], min_severity: Option<Severity>) -> LogInterestSelector {
        LogInterestSelector {
            selector: ComponentSelector {
                moniker_segments: Some(
                    moniker
                        .into_iter()
                        .map(|s| StringSelector::ExactMatch(s.to_string()))
                        .collect(),
                ),
                ..ComponentSelector::EMPTY
            },
            interest: FidlInterest { min_severity, ..FidlInterest::EMPTY },
        }
    }

    async fn assert_severity(event_stream: &mut LogSinkEventStream, severity: Severity) {
        assert_eq!(
            event_stream
                .next()
                .await
                .unwrap()
                .unwrap()
                .into_on_register_interest()
                .unwrap()
                .min_severity
                .unwrap(),
            severity
        );
    }

    fn assert_interests<const N: usize>(
        container: &LogsArtifactsContainer,
        severities: [(Severity, usize); N],
    ) {
        let mut expected_map = BTreeMap::new();
        expected_map.extend(std::array::IntoIter::new(severities).map(|(s, c)| {
            let interest = FidlInterest { min_severity: Some(s), ..FidlInterest::EMPTY };
            (interest.into(), c)
        }));
        assert_eq!(expected_map, container.state.lock().interests);
    }
}
