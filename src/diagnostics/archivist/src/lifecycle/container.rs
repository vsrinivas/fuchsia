// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        identity::ComponentIdentity, inspect::container::InspectArtifactsContainer,
        logs::container::LogsArtifactsContainer,
    },
    diagnostics_data::{self as schema, LifecycleType},
    diagnostics_hierarchy::DiagnosticsHierarchy,
    fuchsia_zircon as zx,
    std::sync::Arc,
};

pub struct LifecycleArtifactsContainer {
    // The time when the Start|Existing event that
    // caused the instantiation of the LifecycleArtifactsContainer
    // was created.
    pub event_timestamp: zx::Time,
}

/// LifecycleDataContainer holds all the information,
/// both metadata and payload, needed to populate a
/// snapshotted Lifecycle schema.
pub struct LifecycleDataContainer {
    pub identity: Arc<ComponentIdentity>,
    pub payload: Option<DiagnosticsHierarchy>,
    pub event_timestamp: zx::Time,
    pub lifecycle_type: schema::LifecycleType,
}

impl LifecycleDataContainer {
    pub fn from_inspect_artifact(
        artifact: &InspectArtifactsContainer,
        identity: Arc<ComponentIdentity>,
    ) -> Self {
        LifecycleDataContainer {
            identity,
            payload: None,
            event_timestamp: artifact.event_timestamp,
            lifecycle_type: LifecycleType::DiagnosticsReady,
        }
    }

    pub fn from_logs_sink_connected_artifact(
        artifact: &LogsArtifactsContainer,
        identity: Arc<ComponentIdentity>,
    ) -> Self {
        LifecycleDataContainer {
            identity,
            payload: None,
            event_timestamp: artifact.event_timestamp,
            lifecycle_type: LifecycleType::LogSinkConnected,
        }
    }

    pub fn from_lifecycle_artifact(
        artifact: &LifecycleArtifactsContainer,
        identity: Arc<ComponentIdentity>,
    ) -> Self {
        LifecycleDataContainer {
            identity,
            payload: None,
            event_timestamp: artifact.event_timestamp,
            lifecycle_type: LifecycleType::Started,
        }
    }
}
