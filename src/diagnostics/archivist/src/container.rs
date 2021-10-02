// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    crate::{
        events::{
            error::EventError,
            types::{ComponentIdentifier, Moniker, UniqueKey, ValidatedSourceIdentity},
        },
        inspect::container::InspectArtifactsContainer,
        lifecycle::container::LifecycleArtifactsContainer,
        logs::{
            budget::BudgetManager, container::LogsArtifactsContainer, multiplex::PinStream,
            stats::LogStreamStats,
        },
        repository::MultiplexerBroker,
        ImmutableString,
    },
    diagnostics_data::{self, LogsData},
    diagnostics_hierarchy::DiagnosticsHierarchy,
    diagnostics_message::MonikerWithUrl,
    fidl_fuchsia_diagnostics::StreamMode,
    fidl_fuchsia_logger::LogInterestSelector,
    fidl_fuchsia_sys_internal::SourceIdentity,
    fuchsia_async as fasync,
    fuchsia_inspect::reader::snapshot::{Snapshot, SnapshotTree},
    fuchsia_inspect_derive::WithInspect,
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    std::{convert::TryFrom, sync::Arc},
    tracing::debug,
};

lazy_static! {
    pub static ref EMPTY_IDENTITY: ComponentIdentity = ComponentIdentity::unknown();
}

pub enum ReadSnapshot {
    Single(Snapshot),
    Tree(SnapshotTree),
    Finished(DiagnosticsHierarchy),
}

#[derive(Clone, Debug, PartialEq)]
pub struct ComponentIdentity {
    /// Relative moniker of the component that this artifacts container
    /// is representing.
    pub relative_moniker: Moniker,

    /// A "rendered" moniker has had `/` inserted between segments and has had instance IDs
    /// appended after colons, this is the form of moniker most familiar to downstream users.
    pub rendered_moniker: String,

    /// The url with which the associated component was launched.
    pub url: String,

    /// In V1, a component topology is able to produce two components with
    /// the same relative moniker. Because of this, we must, in some cases,
    /// differentiate these components using instance ids. The unique key
    /// is conceptually a relative moniker which preserves instance ids.
    pub unique_key: UniqueKey,
}

impl ComponentIdentity {
    pub fn from_identifier_and_url(
        identifier: &ComponentIdentifier,
        url: impl Into<String>,
    ) -> Self {
        ComponentIdentity {
            relative_moniker: identifier.relative_moniker_for_selectors(),
            rendered_moniker: identifier.to_string(),
            unique_key: identifier.unique_key(),
            url: url.into(),
        }
    }

    /// Returns generic metadata, suitable for providing a uniform ID to unattributed data.
    pub fn unknown() -> Self {
        Self::from_identifier_and_url(
            &ComponentIdentifier::Legacy {
                instance_id: "0".to_string(),
                moniker: vec!["UNKNOWN"].into(),
            },
            "fuchsia-pkg://UNKNOWN",
        )
    }
}

impl TryFrom<SourceIdentity> for ComponentIdentity {
    type Error = EventError;
    fn try_from(component: SourceIdentity) -> Result<Self, Self::Error> {
        let component: ValidatedSourceIdentity = ValidatedSourceIdentity::try_from(component)?;
        let mut moniker = component.realm_path;
        moniker.push(component.component_name);
        let id = ComponentIdentifier::Legacy {
            moniker: moniker.into(),
            instance_id: component.instance_id,
        };
        Ok(Self::from_identifier_and_url(&id, component.component_url))
    }
}

impl From<ComponentIdentity> for MonikerWithUrl {
    fn from(identity: ComponentIdentity) -> Self {
        Self { moniker: identity.to_string(), url: identity.url }
    }
}

impl From<&ComponentIdentity> for MonikerWithUrl {
    fn from(identity: &ComponentIdentity) -> Self {
        Self { moniker: identity.to_string(), url: identity.url.clone() }
    }
}

impl std::fmt::Display for ComponentIdentity {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.relative_moniker.fmt(f)
    }
}

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
/// When we receive a stop event for the corresponding component, we set `inspect` and `lifecycle`
/// to `None`, causing `ArchiveAccessor` to ignore this component until it restarts. If `logs` is
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
    /// Container holding the artifacts needed to serve lifecycle data.
    pub lifecycle: Option<LifecycleArtifactsContainer>,
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
        Self { identity, inspect: None, lifecycle: None, logs: None, source_node, is_live: true }
    }

    pub fn new_with_lifecycle(
        identity: Arc<ComponentIdentity>,
        lifecycle: LifecycleArtifactsContainer,
        parent: &fuchsia_inspect::Node,
    ) -> Self {
        let mut new = Self::empty(identity, parent);
        new.lifecycle = Some(lifecycle);
        new
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

    pub fn logs(
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
            let container = Arc::new(LogsArtifactsContainer::new(
                self.identity.clone(),
                interest_selectors,
                stats,
                budget.handle(),
            ));
            budget.add_container(container.clone());
            self.logs = Some(container.clone());
            multiplexers.send(&container);
            container
        }
    }

    /// Return a cursor over messages from this component with the given `mode`.
    pub fn logs_cursor(&self, mode: StreamMode) -> Option<PinStream<Arc<LogsData>>> {
        self.logs.as_ref().map(|l| l.cursor(mode))
    }

    /// Ensure this container is marked as live even if the component it represents had previously
    /// stopped.
    pub fn mark_started(&mut self) {
        debug!(%self.identity, "Marking component as started.");
        self.is_live = true;
        if let Some(logs) = &self.logs {
            logs.mark_started();
        }
    }

    /// Mark this container as stopped -- the component is no longer running and we should not
    /// serve lifecycle or inspect results.
    ///
    /// Sets `inspect` and `lifecycle` to `None` so that this component will be excluded from
    /// accessors for those data types.
    pub fn mark_stopped(&mut self) {
        debug!(%self.identity, "Marking stopped.");
        self.inspect = None;
        self.lifecycle = None;
        self.is_live = false;
        if let Some(logs) = &self.logs {
            logs.mark_stopped();
        }
    }

    /// Returns `true` if the DataRepo should continue holding this container. This container should
    /// be retained as long as we believe the corresponding component is still running or as long as
    /// we still have logs from its execution.
    pub fn should_retain(&self) -> bool {
        let should_retain_logs = self.logs.as_ref().map(|l| l.should_retain()).unwrap_or(false);
        self.is_live || should_retain_logs
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

/// Packet containing a snapshot and all the metadata needed to
/// populate a diagnostics schema for that snapshot.
pub struct SnapshotData {
    // Name of the file that created this snapshot.
    pub filename: ImmutableString,
    // Timestamp at which this snapshot resolved or failed.
    pub timestamp: zx::Time,
    // Errors encountered when processing this snapshot.
    pub errors: Vec<diagnostics_data::Error>,
    // Optional snapshot of the inspect hierarchy, in case reading fails
    // and we have errors to share with client.
    pub snapshot: Option<ReadSnapshot>,
}

impl SnapshotData {
    // Constructs packet that timestamps and packages inspect snapshot for exfiltration.
    pub fn successful(snapshot: ReadSnapshot, filename: ImmutableString) -> SnapshotData {
        SnapshotData {
            filename,
            timestamp: fasync::Time::now().into_zx(),
            errors: Vec::new(),
            snapshot: Some(snapshot),
        }
    }

    // Constructs packet that timestamps and packages inspect snapshot failure for exfiltration.
    pub fn failed(error: diagnostics_data::Error, filename: ImmutableString) -> SnapshotData {
        SnapshotData {
            filename,
            timestamp: fasync::Time::now().into_zx(),
            errors: vec![error],
            snapshot: None,
        }
    }
}
