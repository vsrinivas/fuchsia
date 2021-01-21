// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::ComponentInstance,
        events::{filter::EventFilter, synthesizer::EventSynthesisProvider},
        hooks::{Event, EventPayload},
    },
    async_trait::async_trait,
    std::sync::Arc,
};

pub struct RunningProvider {}

impl RunningProvider {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl EventSynthesisProvider for RunningProvider {
    async fn provide(&self, component: Arc<ComponentInstance>, _filter: EventFilter) -> Vec<Event> {
        match &component.lock_execution().await.runtime {
            // No runtime means the component is not running. Don't synthesize anything.
            None => vec![],
            Some(runtime) => vec![Event::new(
                &component,
                Ok(EventPayload::Running { started_timestamp: runtime.timestamp }),
            )],
        }
    }
}
