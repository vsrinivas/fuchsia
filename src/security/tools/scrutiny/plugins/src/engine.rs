// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{
        collectors, controllers,
        engine::{
            dispatcher::ControllerDispatcher,
            hook::PluginHooks,
            manager::{PluginManager, PluginState},
            plugin::{Plugin, PluginDescriptor},
            scheduler::{CollectorScheduler, CollectorState},
        },
        model::collector::DataCollector,
        model::controller::DataController,
        model::model::DataModel,
    },
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::{Arc, Mutex, RwLock},
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
        manager: Arc<Mutex<PluginManager>>,
    ) -> Self {
        Self {
            desc: PluginDescriptor::new("EnginePlugin".to_string()),
            hooks: PluginHooks::new(
                collectors! {},
                controllers! {
                    "/engine/health/status" => HealthController::default(),
                    "/engine/plugin/list" => PluginListController::new(manager),
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

#[derive(Default)]
pub struct HealthController {}

/// The `HealthController` simply returns a ping. This is used to determine if
/// the service is alive and operating.
impl DataController for HealthController {
    fn query(&self, _: Arc<DataModel>, _: Value) -> Result<Value> {
        Ok(json!({"status" : "ok"}))
    }
    fn description(&self) -> String {
        "Health endpoint that always returns ok.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.health.status - Health endpoint")
            .summary("engine.health.status")
            .description(
                "Health endpoint that always returns ok. \
            This is used mostly by remote endpoints to determine if the \
            connection and the api is available",
            )
            .build()
    }
}

/// Lists all the registered plugins and their states.
struct PluginListController {
    manager: Arc<Mutex<PluginManager>>,
}

#[derive(Serialize, Deserialize)]
struct PluginListEntry {
    name: String,
    state: PluginState,
}

impl PluginListController {
    pub fn new(manager: Arc<Mutex<PluginManager>>) -> Self {
        Self { manager }
    }
}

impl DataController for PluginListController {
    fn query(&self, _: Arc<DataModel>, _query: Value) -> Result<Value> {
        let manager = self.manager.lock().unwrap();
        let plugin_descriptors = manager.plugins();
        let mut plugins = vec![];
        for plugin_desc in plugin_descriptors.iter() {
            let state = manager.state(plugin_desc).unwrap();
            plugins.push(PluginListEntry { name: format!("{}", plugin_desc), state });
        }
        plugins.sort_by(|a, b| a.name.partial_cmp(&b.name).unwrap());
        return Ok(json!(plugins));
    }
    fn description(&self) -> String {
        "Returns a list of all plugins and their state.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.plugin.list - Lists all the plugins")
            .summary("engine.plugin.list")
            .description("Lists all of the available plugins and their state")
            .build()
    }
}

/// Displays basic stats from the model controller.
#[derive(Default)]
struct ModelStatsController {}

#[derive(Serialize, Deserialize)]
struct ModelStats {
    components: usize,
    packages: usize,
    manifests: usize,
    routes: usize,
    #[serde(rename = "zbi sections")]
    zbi_sections: usize,
    #[serde(rename = "bootfs files")]
    bootfs_files: usize,
}

impl DataController for ModelStatsController {
    fn query(&self, model: Arc<DataModel>, _query: Value) -> Result<Value> {
        let mut zbi_sections = 0;
        let mut bootfs_files = 0;

        if let Some(zbi) = &*model.zbi().read().unwrap() {
            zbi_sections = zbi.sections.len();
            bootfs_files = zbi.bootfs.len();
        }

        let stats = ModelStats {
            components: model.components().read().unwrap().len(),
            packages: model.packages().read().unwrap().len(),
            manifests: model.manifests().read().unwrap().len(),
            routes: model.routes().read().unwrap().len(),
            zbi_sections,
            bootfs_files,
        };
        Ok(json!(stats))
    }
    fn description(&self) -> String {
        "Returns aggregated model statistics.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.model.stats - Lists important model statistics")
            .summary("engine.model.stats")
            .description(
                "Lists the number of: components, packages, manifests, \
            routes, zbi sections and bootfs files currently loaded in the model.",
            )
            .build()
    }
}

/// Displays a list of all the collectors.
struct CollectorListController {
    scheduler: Arc<Mutex<CollectorScheduler>>,
}

impl CollectorListController {
    fn new(scheduler: Arc<Mutex<CollectorScheduler>>) -> Self {
        Self { scheduler }
    }
}

#[derive(Serialize, Deserialize)]
struct CollectorListEntry {
    name: String,
    state: CollectorState,
}

impl DataController for CollectorListController {
    fn query(&self, _model: Arc<DataModel>, _query: Value) -> Result<Value> {
        let scheduler = self.scheduler.lock().unwrap();
        let mut collectors = vec![];
        for (handle, name) in scheduler.collectors_all() {
            let state = scheduler.state(&handle).unwrap();
            collectors.push(CollectorListEntry { name, state });
        }
        collectors.sort_by(|a, b| a.name.partial_cmp(&b.name).unwrap());
        Ok(json!(collectors))
    }
    fn description(&self) -> String {
        "Returns a list of all loaded data collectors.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.controller.list - Lists all of the data controllers")
            .summary("engine.controller.list")
            .description(
                "Lists all of the controllers that are currently loaded \
            in Scrutiny. This command provides internal inspection of the Scrutiny \
            engine and is helpful for debugging plugin issues.",
            )
            .build()
    }
}

/// Runs all of the collectors when called.
struct CollectorSchedulerController {
    scheduler: Arc<Mutex<CollectorScheduler>>,
}

impl CollectorSchedulerController {
    fn new(scheduler: Arc<Mutex<CollectorScheduler>>) -> Self {
        Self { scheduler }
    }
}

impl DataController for CollectorSchedulerController {
    fn query(&self, _model: Arc<DataModel>, _query: Value) -> Result<Value> {
        if self.scheduler.lock().unwrap().schedule().is_ok() {
            Ok(json!({"status": "ok"}))
        } else {
            Ok(json!({"status": "failed"}))
        }
    }
    fn description(&self) -> String {
        "Schedules all data collectors to run.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.collector.schedule - Runs all of the DataCollectors")
            .summary("engine.collector.schedule")
            .description(
                "Schedules all the loaded data collectors to run. This \
            repopulates the DataModel with all of the data provided by the collectors. \
            This is useful if you want to force the model to be refreshed with the \
            latest system data.",
            )
            .build()
    }
}

/// Lists all of the controllers.
struct ControllerListController {
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
}

impl ControllerListController {
    fn new(dispatcher: Arc<RwLock<ControllerDispatcher>>) -> Self {
        Self { dispatcher }
    }
}

impl DataController for ControllerListController {
    fn query(&self, _model: Arc<DataModel>, _query: Value) -> Result<Value> {
        Ok(json!(self.dispatcher.read().unwrap().controllers_all()))
    }
    fn description(&self) -> String {
        "Lists all loaded data collectors.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.collector.list- List all of the DataCollectors")
            .summary("engine.collector.list")
            .description(
                "Lists all of the loaded Data Collectors. This can be\
            useful for debugging plugins.",
            )
            .build()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, scrutiny::model::collector::DataCollector, scrutiny::model::model::Component,
        scrutiny::plugin, std::boxed::Box, tempfile::tempdir, uuid::Uuid,
    };

    plugin!(FakePlugin, PluginHooks::new(collectors! {}, controllers! {}), vec![]);

    struct FakeCollector {}
    impl DataCollector for FakeCollector {
        fn collect(&self, _model: Arc<DataModel>) -> Result<()> {
            Ok(())
        }
    }

    fn data_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
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

    #[test]
    fn test_plugin_list_controller() {
        let model = data_model();
        let manager = plugin_manager(model.clone());
        manager.lock().unwrap().register(Box::new(FakePlugin::new())).unwrap();
        let plugin_list = PluginListController::new(manager.clone());
        let response = plugin_list.query(model.clone(), json!("")).unwrap();
        let list: Vec<PluginListEntry> = serde_json::from_value(response).unwrap();
        assert_eq!(list.len(), 1);
    }

    #[test]
    fn test_model_stats_controller() {
        let model = data_model();
        let model_stats = ModelStatsController::default();
        assert_eq!(model_stats.query(model.clone(), json!("")).is_ok(), true);
        model.components().write().unwrap().push(Component {
            id: 1,
            url: "".to_string(),
            version: 1,
            inferred: true,
        });
        let response = model_stats.query(model.clone(), json!("")).unwrap();
        let stats: ModelStats = serde_json::from_value(response).unwrap();
        assert_eq!(stats.components, 1);
        assert_eq!(stats.packages, 0);
        assert_eq!(stats.routes, 0);
        assert_eq!(stats.manifests, 0);
        assert_eq!(stats.zbi_sections, 0);
        assert_eq!(stats.bootfs_files, 0);
    }

    #[test]
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

    #[test]
    fn test_collector_list_controller() {
        let model = data_model();
        let scheduler = scheduler(model.clone());
        scheduler.lock().unwrap().add(Uuid::new_v4(), "foo", Arc::new(FakeCollector {}));
        let collector_list = CollectorListController::new(scheduler);
        let response = collector_list.query(model.clone(), json!("")).unwrap();
        let list: Vec<CollectorListEntry> = serde_json::from_value(response).unwrap();
        assert_eq!(list.len(), 1);
    }

    #[test]
    fn test_collector_scheduler_controller() {
        let model = data_model();
        let scheduler = scheduler(model.clone());
        let schedule_controller = CollectorSchedulerController::new(scheduler);
        let response = schedule_controller.query(model.clone(), json!("")).unwrap();
        assert_eq!(response, json!({"status": "ok"}));
    }
}
