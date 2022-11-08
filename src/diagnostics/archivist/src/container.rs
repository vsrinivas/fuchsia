// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{
        identity::ComponentIdentity,
        logs::{
            budget::BudgetManager, container::LogsArtifactsContainer, multiplex::PinStream,
            repository::MultiplexerBroker, stats::LogStreamStats,
        },
    },
    diagnostics_data::{self, LogsData},
    fidl_fuchsia_diagnostics::{LogInterestSelector, StreamMode},
    fuchsia_inspect_derive::WithInspect,
    fuchsia_trace as ftrace,
    std::sync::Arc,
};

/// Holds all diagnostics data artifacts for a given component in the topology.
///
/// While most members are public for convenience, other data types may be added in the future and
/// so this is marked with `#[non_exhaustive]`.
///
/// # Lifecycle
///
/// Each instance is held by the DataRepo as long as the corresponding component is running or this
/// struct has `logs` that are non-empty.
///
/// When we receive a stop event for the corresponding component, we set `inspect` to `None`,
/// causing `ArchiveAccessor` to ignore this component until it restarts. If `logs` is
/// empty at that point, this struct should be removed from the DataRepo.
///
/// See [`ComponentDiagnostics::mark_stopped`] and
/// [`ComponentDiagnostics::should_retain`] for the implementations of this behavior.
#[non_exhaustive]
pub struct ComponentDiagnostics {
    /// Container holding the artifacts needed to uniquely identify
    /// a component on the system.
    pub identity: Arc<ComponentIdentity>,
    /// Container holding cached log messages and interest dispatchers.
    pub logs: Option<Arc<LogsArtifactsContainer>>,
    /// Holds the state for `root/sources/MONIKER/*` in Archivist's inspect.
    pub source_node: fuchsia_inspect::Node,
}

impl ComponentDiagnostics {
    pub async fn new_with_logs(
        identity: Arc<ComponentIdentity>,
        parent: &fuchsia_inspect::Node,
        budget: &BudgetManager,
        interest_selectors: &[LogInterestSelector],
        multiplexers: &mut MultiplexerBroker,
    ) -> (Self, Arc<LogsArtifactsContainer>) {
        let mut new = Self::empty(identity, parent);
        let logs = new.logs(budget, interest_selectors, multiplexers).await;
        (new, logs)
    }

    pub async fn logs(
        &mut self,
        budget: &BudgetManager,
        interest_selectors: &[LogInterestSelector],
        multiplexers: &mut MultiplexerBroker,
    ) -> Arc<LogsArtifactsContainer> {
        if let Some(logs) = &self.logs {
            logs.clone()
        } else {
            let stats = LogStreamStats::default()
                .with_inspect(&self.source_node, "logs")
                .expect("failed to attach component log stats");
            let container = Arc::new(
                LogsArtifactsContainer::new(
                    self.identity.clone(),
                    interest_selectors,
                    stats,
                    budget.handle(),
                )
                .await,
            );
            budget.add_container(container.clone()).await;
            self.logs = Some(container.clone());
            multiplexers.send(&container).await;
            container
        }
    }

    /// Return a cursor over messages from this component with the given `mode`.
    pub fn logs_cursor(
        &self,
        mode: StreamMode,
        parent_trace_id: ftrace::Id,
    ) -> Option<PinStream<Arc<LogsData>>> {
        self.logs.as_ref().map(|l| l.cursor(mode, parent_trace_id))
    }

    /// Returns `true` if the DataRepo should continue holding this container. This container should
    /// be retained as long as we believe the corresponding component is still running or as long as
    /// we still have logs from its execution.
    pub async fn should_retain(&self) -> bool {
        match self.logs.as_ref() {
            Some(log) => log.should_retain().await,
            None => false,
        }
    }

    /// Ensure that no new log messages can be consumed from the corresponding component, causing
    /// all pending subscriptions to terminate. Used during Archivist shutdown to ensure a full
    /// flush of available logs.
    pub fn terminate_logs(&self) {
        if let Some(logs) = &self.logs {
            logs.terminate();
        }
    }

    fn empty(identity: Arc<ComponentIdentity>, parent: &fuchsia_inspect::Node) -> Self {
        let source_node = parent.create_child(identity.relative_moniker.join("/"));
        source_node.record_string("url", &identity.url);
        Self { identity, logs: None, source_node }
    }
}
