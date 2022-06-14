// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        hooks::{Event, EventType, Hook, HooksRegistration},
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
            vec![
                EventType::DirectoryReady,
                EventType::CapabilityRequested,
                EventType::CapabilityRouted,
                EventType::Destroyed,
                EventType::Started,
                EventType::Stopped,
            ],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }
}

#[async_trait]
impl Hook for EventLogger {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        info!("{}", event);
        Ok(())
    }
}
