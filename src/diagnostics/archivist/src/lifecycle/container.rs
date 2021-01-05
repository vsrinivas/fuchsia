// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{container::ComponentIdentity, inspect::container::InspectArtifactsContainer},
    diagnostics_data::{self as schema, LifecycleType},
    diagnostics_hierarchy::{DiagnosticsHierarchy, Property},
    fuchsia_zircon as zx,
    std::sync::Arc,
};

pub struct LifecycleArtifactsContainer {
    // The time when the Start|Existing event that
    // caused the instantiation of the LifecycleArtifactsContainer
    // was created.
    pub event_timestamp: zx::Time,
    // Optional time when the component who the instantiating lifecycle
    // event was about was started. If None, it is the same as the
    // event_timestamp.
    pub component_start_time: Option<zx::Time>,
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

    pub fn from_lifecycle_artifact(
        artifact: &LifecycleArtifactsContainer,
        identity: Arc<ComponentIdentity>,
    ) -> Self {
        if let Some(component_start_time) = artifact.component_start_time {
            let payload = DiagnosticsHierarchy::new(
                "root",
                vec![Property::Int(
                    "component_start_time".to_string(),
                    component_start_time.into_nanos(),
                )],
                vec![],
            );

            LifecycleDataContainer {
                identity,
                payload: Some(payload),
                event_timestamp: artifact.event_timestamp,
                lifecycle_type: LifecycleType::Running,
            }
        } else {
            LifecycleDataContainer {
                identity,
                payload: None,
                event_timestamp: artifact.event_timestamp,
                lifecycle_type: LifecycleType::Started,
            }
        }
    }
}
