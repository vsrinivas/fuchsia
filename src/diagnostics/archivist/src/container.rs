// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{
        identity::ComponentIdentity,
        inspect::container::InspectArtifactsContainer,
        logs::{
            budget::BudgetManager, container::LogsArtifactsContainer, multiplex::PinStream,
            stats::LogStreamStats,
        },
        repository::MultiplexerBroker,
    },
    diagnostics_data::{self, LogsData},
    fidl_fuchsia_diagnostics::{LogInterestSelector, StreamMode},
    fuchsia_inspect_derive::WithInspect,
    std::sync::Arc,
    tracing::debug,
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
/// See [`ComponentDiagnostics::mark_stopped`], [`ComponentDiagnostics::mark_started`], and
/// [`ComponentDiagnostics::should_retain`] for the implementations of this behavior.
#[non_exhaustive]
pub struct ComponentDiagnostics {
    /// Container holding the artifacts needed to uniquely identify
    /// a component on the system.
    pub identity: Arc<ComponentIdentity>,
    /// Container holding the artifacts needed to serve inspect data.
    /// If absent, this is interpereted as a component existing, but not
    /// hosting diagnostics data.
    pub inspect: Option<InspectArtifactsContainer>,
    /// Container holding cached log messages and interest dispatchers.
    pub logs: Option<Arc<LogsArtifactsContainer>>,
    /// Holds the state for `root/sources/MONIKER/*` in Archivist's inspect.
    pub source_node: fuchsia_inspect::Node,
    /// Whether the component this represents is still running or not. Components which have
    /// stopped have their containers retained for as long as they still have log messages we want.
    is_live: bool,
}

impl ComponentDiagnostics {
    pub fn empty(identity: Arc<ComponentIdentity>, parent: &fuchsia_inspect::Node) -> Self {
        let source_node = parent.create_child(identity.relative_moniker.join("/"));
        source_node.record_string("url", &identity.url);
        Self { identity, inspect: None, logs: None, source_node, is_live: true }
    }

    pub fn new_with_inspect(
        identity: Arc<ComponentIdentity>,
        inspect: InspectArtifactsContainer,
        parent: &fuchsia_inspect::Node,
    ) -> Self {
        let mut new = Self::empty(identity, parent);
        new.inspect = Some(inspect);
        new
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
    pub fn logs_cursor(&self, mode: StreamMode) -> Option<PinStream<Arc<LogsData>>> {
        self.logs.as_ref().map(|l| l.cursor(mode))
    }

    /// Ensure this container is marked as live even if the component it represents had previously
    /// stopped.
    pub async fn mark_started(&mut self) {
        debug!(%self.identity, "Marking component as started.");
        self.is_live = true;
        if let Some(logs) = &self.logs {
            logs.mark_started().await;
        }
    }

    /// Mark this container as stopped -- the component is no longer running and we should not
    /// serve inspect results.
    ///
    /// Sets `inspect` to `None` so that this component will be excluded from
    /// accessors for those data types.
    pub async fn mark_stopped(&mut self) {
        debug!(%self.identity, "Marking stopped.");
        self.inspect = None;
        self.is_live = false;
        if let Some(logs) = &self.logs {
            logs.mark_stopped().await;
        }
    }

    /// Returns `true` if the DataRepo should continue holding this container. This container should
    /// be retained as long as we believe the corresponding component is still running or as long as
    /// we still have logs from its execution.
    pub async fn should_retain(&self) -> bool {
        match self.logs.as_ref() {
            Some(log) => self.is_live || log.should_retain().await,
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
}
