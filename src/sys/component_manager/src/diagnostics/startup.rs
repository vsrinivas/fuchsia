// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::error::ModelError,
        model::hooks::{Event, EventPayload, EventType, HasEventType, Hook, HooksRegistration},
    },
    async_trait::async_trait,
    fuchsia_inspect as inspect, fuchsia_zircon as zx,
    lazy_static::lazy_static,
    moniker::AbsoluteMoniker,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc, Weak,
    },
};

lazy_static! {
    static ref MONIKER: inspect::StringReference<'static> = "moniker".into();
    static ref START_TIME: inspect::StringReference<'static> = "time".into();
}

/// Allows to track startup times of components that start early in the boot process, within 5s of
/// the given time.
pub struct ComponentEarlyStartupTimeStats {
    node: inspect::Node,
    after_five_seconds: zx::Time,
    next_id: AtomicUsize,
}

impl ComponentEarlyStartupTimeStats {
    /// Creates a new startup time tracker. Data will be written to the given inspect node.
    pub fn new(node: inspect::Node, initial_time: zx::Time) -> Self {
        Self {
            node,
            after_five_seconds: initial_time + zx::Duration::from_seconds(5),
            next_id: AtomicUsize::new(0),
        }
    }

    /// Provides the hook events that are needed to work.
    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "ComponentEarlyStartupTimeStats",
            vec![EventType::Started],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_component_started(
        self: &Arc<Self>,
        moniker: &AbsoluteMoniker,
        start_time: zx::Time,
    ) {
        if start_time > self.after_five_seconds {
            return;
        }
        let id = self.next_id.fetch_add(1, Ordering::SeqCst);
        self.node.record_child(id.to_string(), |node| {
            node.record_string(&*MONIKER, moniker.to_string());
            node.record_int(&*START_TIME, start_time.into_nanos());
        });
    }
}

#[async_trait]
impl Hook for ComponentEarlyStartupTimeStats {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        let target_moniker = event
            .target_moniker
            .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
        match event.event_type() {
            EventType::Started => {
                if let Some(EventPayload::Started { runtime, .. }) = event.result.as_ref().ok() {
                    self.on_component_started(target_moniker, runtime.start_time).await;
                }
            }
            _ => {}
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::model::{
        component::StartReason,
        model::Model,
        starter::Starter,
        testing::test_helpers::{component_decl_with_test_runner, ActionsTest},
    };
    use cm_rust_testing::ComponentDeclBuilder;
    use fuchsia_inspect::assert_data_tree;

    #[fuchsia::test]
    async fn tracks_started_components() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        let inspector = inspect::Inspector::new();
        let stats = Arc::new(ComponentEarlyStartupTimeStats::new(
            inspector.root().create_child("start_times"),
            zx::Time::get_monotonic(),
        ));
        test.model.root().hooks.install(stats.hooks()).await;

        let root_timestamp =
            start_and_get_timestamp(test.model.clone(), vec![].into()).await.into_nanos();
        let a_timestamp =
            start_and_get_timestamp(test.model.clone(), vec!["a"].into()).await.into_nanos();
        let b_timestamp =
            start_and_get_timestamp(test.model.clone(), vec!["a", "b"].into()).await.into_nanos();

        assert_data_tree!(inspector, root: {
            start_times: {
                "0": {
                    moniker: "/",
                    time: root_timestamp,
                },
                "1": {
                    moniker: "/a",
                    time: a_timestamp,
                },
                "2": {
                    moniker: "/a/b",
                    time: b_timestamp,
                }
            }
        });
    }

    #[fuchsia::test]
    async fn doesnt_track_components_started_more_than_five_seconds_after_the_given_time() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        let inspector = inspect::Inspector::new();
        let stats = Arc::new(ComponentEarlyStartupTimeStats::new(
            inspector.root().create_child("start_times"),
            zx::Time::get_monotonic() - zx::Duration::from_seconds(6),
        ));
        test.model.root().hooks.install(stats.hooks()).await;

        let _root_timestamp =
            start_and_get_timestamp(test.model.clone(), vec![].into()).await.into_nanos();
        let _a_timestamp =
            start_and_get_timestamp(test.model.clone(), vec!["a"].into()).await.into_nanos();
        let _b_timestamp =
            start_and_get_timestamp(test.model.clone(), vec!["a", "b"].into()).await.into_nanos();

        assert_data_tree!(inspector, root: {
            start_times: {
            }
        });
    }

    async fn start_and_get_timestamp(model: Arc<Model>, moniker: AbsoluteMoniker) -> zx::Time {
        let component =
            model.start_instance(&moniker, &StartReason::Root).await.expect("failed to bind");
        let exec = component.lock_execution().await;
        exec.runtime.as_ref().unwrap().timestamp
    }
}
