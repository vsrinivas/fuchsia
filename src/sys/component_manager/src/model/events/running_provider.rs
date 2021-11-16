// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        events::synthesizer::{EventSynthesisProvider, ExtendedComponent},
        hooks::{Event, EventPayload},
    },
    ::routing::event::EventFilter,
    async_trait::async_trait,
};

pub struct RunningProvider {}

impl RunningProvider {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl EventSynthesisProvider for RunningProvider {
    async fn provide(&self, component: ExtendedComponent, _filter: &EventFilter) -> Vec<Event> {
        let component = match component {
            ExtendedComponent::ComponentManager => return vec![],
            ExtendedComponent::ComponentInstance(component) => component,
        };
        let execution = component.lock_execution().await;
        match execution.runtime.as_ref() {
            // No runtime means the component is not running. Don't synthesize anything.
            None => vec![],
            Some(runtime) => vec![Event::new(
                &component,
                Ok(EventPayload::Running { started_timestamp: runtime.timestamp }),
            )],
        }
    }
}
