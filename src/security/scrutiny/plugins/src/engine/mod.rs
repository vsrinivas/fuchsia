// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod controller;

use {
    crate::engine::controller::{
        collector::{CollectorListController, CollectorSchedulerController},
        controller::ControllerListController,
        health::HealthController,
        model::{ModelConfigController, ModelStatsController},
        plugin::PluginListController,
    },
    scrutiny::engine::{
        dispatcher::ControllerDispatcher, manager::PluginManager, scheduler::CollectorScheduler,
    },
    scrutiny::prelude::*,
    std::sync::{Arc, Mutex, RwLock, Weak},
};

/// The `EnginePlugin` allows introspection into the Scrutiny engine
/// through a plugin. This allows users to inspect the abstracted state of
/// the system such as seeing what collectors, controllers and plugins are
/// active. It also allows the user to reschedule collector tasks, disable and
/// enable plugins etc.
pub struct EnginePlugin {
    desc: PluginDescriptor,
    hooks: PluginHooks,
    deps: Vec<PluginDescriptor>,
}

impl EnginePlugin {
    pub fn new(
        scheduler: Arc<Mutex<CollectorScheduler>>,
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
        manager: Weak<Mutex<PluginManager>>,
    ) -> Self {
        Self {
            desc: PluginDescriptor::new("EnginePlugin".to_string()),
            hooks: PluginHooks::new(
                collectors! {},
                controllers! {
                    "/engine/health/status" => HealthController::default(),
                    "/engine/plugin/list" => PluginListController::new(manager),
                    "/engine/model/config" => ModelConfigController::default(),
                    "/engine/model/stats" => ModelStatsController::default(),
                    "/engine/collector/list" => CollectorListController::new(scheduler.clone()),
                    "/engine/controller/list" => ControllerListController::new(dispatcher),
                    "/engine/collector/schedule" => CollectorSchedulerController::new(scheduler.clone()),
                },
            ),
            deps: vec![],
        }
    }
}

impl Plugin for EnginePlugin {
    fn descriptor(&self) -> &PluginDescriptor {
        &self.desc
    }
    fn dependencies(&self) -> &Vec<PluginDescriptor> {
        &self.deps
    }
    fn hooks(&mut self) -> &PluginHooks {
        &self.hooks
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::core::collection::{Component, ComponentSource, Components},
        crate::engine::controller::{
            collector::CollectorListEntry, model::ModelStats, plugin::PluginListEntry,
        },
        anyhow::Result,
        scrutiny::model::collector::DataCollector,
        scrutiny::model::model::DataModel,
        scrutiny::plugin,
        scrutiny_testing::fake::*,
        serde_json::json,
        std::boxed::Box,
        std::collections::HashSet,
        url::Url,
        uuid::Uuid,
    };

    plugin!(FakePlugin, PluginHooks::new(collectors! {}, controllers! {}), vec![]);

    struct FakeCollector {}
    impl DataCollector for FakeCollector {
        fn collect(&self, _model: Arc<DataModel>) -> Result<()> {
            Ok(())
        }
    }

    fn data_model() -> Arc<DataModel> {
        fake_data_model()
    }

    fn dispatcher(model: Arc<DataModel>) -> Arc<RwLock<ControllerDispatcher>> {
        Arc::new(RwLock::new(ControllerDispatcher::new(model)))
    }

    fn scheduler(model: Arc<DataModel>) -> Arc<Mutex<CollectorScheduler>> {
        Arc::new(Mutex::new(CollectorScheduler::new(model)))
    }

    fn plugin_manager(model: Arc<DataModel>) -> Arc<Mutex<PluginManager>> {
        let dispatcher = dispatcher(model.clone());
        let scheduler = scheduler(model.clone());
        Arc::new(Mutex::new(PluginManager::new(scheduler, dispatcher)))
    }

    #[fuchsia::test]
    fn test_plugin_list_controller() {
        let model = data_model();
        let manager = plugin_manager(model.clone());
        manager.lock().unwrap().register(Box::new(FakePlugin::new())).unwrap();
        let plugin_list = PluginListController::new(Arc::downgrade(&manager));
        let response = plugin_list.query(model.clone(), json!("")).unwrap();
        let list: Vec<PluginListEntry> = serde_json::from_value(response).unwrap();
        assert_eq!(list.len(), 1);
    }

    #[fuchsia::test]
    fn test_model_stats_controller() {
        let model = data_model();
        let model_stats = ModelStatsController::default();
        assert_eq!(model_stats.query(model.clone(), json!("")).is_ok(), true);
        model
            .set(Components::new(vec![Component {
                id: 1,
                url: Url::parse("fuchsia-pkg://fuchsia.com/test#meta/test.cmx").unwrap(),
                version: 1,
                source: ComponentSource::Inferred,
            }]))
            .unwrap();
        let response = model_stats.query(model.clone(), json!("")).unwrap();
        let stats: ModelStats = serde_json::from_value(response).unwrap();
        assert_eq!(stats.components, 1);
        assert_eq!(stats.packages, 0);
        assert_eq!(stats.routes, 0);
        assert_eq!(stats.manifests, 0);
        assert_eq!(stats.zbi_sections, 0);
        assert_eq!(stats.bootfs_files, 0);
    }

    #[fuchsia::test]
    fn test_model_env_controller() {
        let model = data_model();
        let model_stats = ModelConfigController::default();
        assert_eq!(model_stats.query(model.clone(), json!("")).is_ok(), true);
    }

    #[fuchsia::test]
    fn test_controller_list_controller() {
        let model = data_model();
        let dispatcher = dispatcher(model.clone());
        dispatcher
            .write()
            .unwrap()
            .add(Uuid::new_v4(), "/foo/bar".to_string(), Arc::new(ModelStatsController::default()))
            .unwrap();
        let controller_list = ControllerListController::new(dispatcher);
        let response = controller_list.query(model.clone(), json!("")).unwrap();
        let controllers: Vec<String> = serde_json::from_value(response).unwrap();
        assert_eq!(controllers, vec!["/foo/bar".to_string()]);
    }

    #[fuchsia::test]
    fn test_collector_list_controller() {
        let model = data_model();
        let scheduler = scheduler(model.clone());
        scheduler.lock().unwrap().add(
            Uuid::new_v4(),
            "foo",
            HashSet::new(),
            Arc::new(FakeCollector {}),
        );
        let collector_list = CollectorListController::new(scheduler);
        let response = collector_list.query(model.clone(), json!("")).unwrap();
        let list: Vec<CollectorListEntry> = serde_json::from_value(response).unwrap();
        assert_eq!(list.len(), 1);
    }

    #[fuchsia::test]
    fn test_collector_scheduler_controller() {
        let model = data_model();
        let scheduler = scheduler(model.clone());
        let schedule_controller = CollectorSchedulerController::new(scheduler);
        let response = schedule_controller.query(model.clone(), json!("")).unwrap();
        assert_eq!(response, json!({"status": "ok"}));
    }
}
