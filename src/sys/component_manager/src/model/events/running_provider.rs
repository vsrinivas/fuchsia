// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        events::{filter::EventFilter, synthesizer::EventSynthesisProvider},
        hooks::{Event, EventPayload},
        realm::Realm,
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
    async fn provide(&self, realm: Arc<Realm>, _filter: EventFilter) -> Vec<Event> {
        match &realm.lock_execution().await.runtime {
            // No runtime means the component is not running. Don't synthesize anything.
            None => vec![],
            Some(runtime) => vec![Event::new_with_timestamp(
                realm.abs_moniker.clone(),
                Ok(EventPayload::Running),
                runtime.timestamp,
            )],
        }
    }
}
