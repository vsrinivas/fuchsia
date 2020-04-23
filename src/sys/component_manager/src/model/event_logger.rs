// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
            realm::BindReason,
        },
    },
    async_trait::async_trait,
    log::info,
    std::sync::{Arc, Weak},
};

pub struct EventLogger;

impl EventLogger {
    pub fn new() -> Self {
        Self
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "EventLogger",
            vec![EventType::CapabilityRouted, EventType::Started],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_capability_routed(
        self: Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        source: &CapabilitySource,
    ) {
        match source {
            CapabilitySource::Component { capability, realm } => {
                info!(
                    "[Routed] '{}' from '{}' to '{}'",
                    capability.source_id(),
                    realm.moniker,
                    target_moniker
                );
            }
            CapabilitySource::Framework { capability, .. } => {
                info!(
                    "[Routed] '{}' from framework to '{}'",
                    capability.source_id(),
                    target_moniker
                );
            }
        }
    }

    async fn on_started(
        self: Arc<Self>,
        target_moniker: &AbsoluteMoniker,
        bind_reason: &BindReason,
    ) {
        info!("[Started] '{}' because {}", target_moniker, bind_reason.to_string());
    }
}

#[async_trait]
impl Hook for EventLogger {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        // TODO(fxb/49787): Report failed routing with advice on how to resolve the issue.
        match &event.result {
            Ok(EventPayload::CapabilityRouted { source, .. }) => {
                self.on_capability_routed(&event.target_moniker, &source).await;
            }
            Ok(EventPayload::Started { bind_reason, .. }) => {
                self.on_started(&event.target_moniker, &bind_reason).await;
            }
            _ => {}
        }
        Ok(())
    }
}
