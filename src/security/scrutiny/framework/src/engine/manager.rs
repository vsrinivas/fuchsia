// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        dispatcher::ControllerDispatcher,
        plugin::{Plugin, PluginDescriptor},
        scheduler::CollectorScheduler,
    },
    anyhow::Result,
    log::{error, info},
    serde::{Deserialize, Serialize},
    std::fmt,
    std::{
        boxed::Box,
        collections::{HashMap, HashSet},
        sync::{Arc, Mutex, RwLock},
    },
    thiserror::Error,
    uuid::Uuid,
};

#[derive(Error, Debug)]
pub enum PluginError {
    #[error("Plugin {0} could not be found")]
    NotFound(PluginDescriptor),
    #[error("Plugin {0} is already registered")]
    RegisterCollision(PluginDescriptor),
    #[error("Plugin {0} requires plugin {1}")]
    DependencyRequired(PluginDescriptor, PluginDescriptor),
    #[error("Plugin {0} is already loaded")]
    AlreadyLoaded(PluginDescriptor),
    #[error("Plugin {0} is already unloaded")]
    AlreadyUnloaded(PluginDescriptor),
    #[error("Plugin {0} is in use")]
    InUse(PluginDescriptor),
    #[error("Plugin {0} is in use by {1}")]
    InUseBy(PluginDescriptor, PluginDescriptor),
    #[error("Plugin {0} is in use by multiple plugins {:?}")]
    InUseByMany(PluginDescriptor, HashSet<PluginDescriptor>),
}

/// Simple utility wrapper that logs and error and returns it.
fn log_error(error: PluginError) -> Result<()> {
    error!("{:?}", error);
    Err(error.into())
}

/// `PluginState` informs the `PluginManager` whether the plugin has its
/// `DataCollector` and `DataController` instances currently hooked or not.
#[derive(Debug, PartialEq, Clone, Serialize, Deserialize)]
pub enum PluginState {
    Loaded,
    Unloaded,
}

impl fmt::Display for PluginState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PluginState::Loaded => write!(f, "Loaded"),
            PluginState::Unloaded => write!(f, "Unloaded"),
        }
    }
}

/// `PluginInstance` holds all the information the `PluginManager` needs to
/// keep track of a registered `Plugin`.
struct PluginInstance {
    pub plugin: Box<dyn Plugin>,
    pub state: PluginState,
    pub consumers: HashSet<PluginDescriptor>,
    pub instance_id: Uuid,
}

impl PluginInstance {
    /// All `PluginInstance` types started in an unloaded state.
    pub fn new(plugin: Box<dyn Plugin>) -> Self {
        Self {
            plugin: plugin,
            state: PluginState::Unloaded,
            consumers: HashSet::new(),
            instance_id: Uuid::new_v4(),
        }
    }
}

/// The `PluginManager` is responsible for registering and loading plugins
/// along with their declared dependencies. It is responsible for assigning the
/// plugins a `PluginInstance` and delegating collection to the
/// `CollectorWorkerScheduler` and controlling to the `ControllerDispatcher`.
pub struct PluginManager {
    plugins: HashMap<PluginDescriptor, PluginInstance>,
    scheduler: Arc<Mutex<CollectorScheduler>>,
    dispatcher: Arc<RwLock<ControllerDispatcher>>,
}

impl PluginManager {
    pub fn new(
        scheduler: Arc<Mutex<CollectorScheduler>>,
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
    ) -> Self {
        Self { plugins: HashMap::new(), scheduler: scheduler, dispatcher: dispatcher }
    }

    /// Utility to register and load a plugin in one line.
    pub fn register_and_load(&mut self, plugin: Box<dyn Plugin>) -> Result<()> {
        let desc = plugin.descriptor().clone();
        self.register(plugin)?;
        self.load(&desc)
    }

    /// Attempts to add a new plugin to the `PluginManager`. This will fail if
    /// the plugin is already registered.
    pub fn register(&mut self, plugin: Box<dyn Plugin>) -> Result<()> {
        let desc = plugin.descriptor();
        if self.plugins.contains_key(desc) {
            return log_error(PluginError::RegisterCollision(desc.clone()));
        }
        info!("Plugin: {} Registered", plugin.descriptor());
        self.plugins.insert(plugin.descriptor().clone(), PluginInstance::new(plugin));
        Ok(())
    }

    /// Attempts to unregister a plugin from the `PluginManager`. This will
    /// fail if the plugin is loaded or is unregistered.
    pub fn unregister(&mut self, desc: &PluginDescriptor) -> Result<()> {
        if let Some(plugin_instance) = self.plugins.get(desc) {
            if plugin_instance.state == PluginState::Loaded {
                return log_error(PluginError::InUse(desc.clone()));
            }
        }
        if let Some(_) = self.plugins.remove(desc) {
            info!("Plugin: {} Unregistered", desc);
            Ok(())
        } else {
            log_error(PluginError::NotFound(desc.clone()))
        }
    }

    /// Attempts to load the plugin. This will fail if the plugin is not
    /// in a loaded state, not registered or a dependency is not loaded.
    pub fn load(&mut self, desc: &PluginDescriptor) -> Result<()> {
        info!("Plugin: {} Loading", desc);
        let mut deps = Vec::new();
        if let Some(plugin_instance) = self.plugins.get(desc) {
            if plugin_instance.state == PluginState::Loaded {
                return log_error(PluginError::AlreadyLoaded(desc.clone()));
            }
            deps.extend(plugin_instance.plugin.dependencies().clone());
        } else {
            return log_error(PluginError::NotFound(desc.clone()));
        }
        // Verify all the dependencies are loaded for the plugin.
        for dep_desc in deps.iter() {
            if let Some(dep_instance) = self.plugins.get(&dep_desc) {
                if dep_instance.state != PluginState::Loaded {
                    return log_error(PluginError::InUseBy(desc.clone(), dep_desc.clone()));
                }
            } else {
                return log_error(PluginError::DependencyRequired(desc.clone(), dep_desc.clone()));
            }
        }

        // Attach the consumer tag to all dependencies.
        for dep_desc in deps.iter() {
            let dep_instance = self.plugins.get_mut(&dep_desc).unwrap();
            dep_instance.consumers.insert(desc.clone());
        }

        let plugin_instance = self.plugins.get_mut(desc).unwrap();
        let hooks = plugin_instance.plugin.hooks();
        // Hook all the collectors into the worker scheduler.
        let mut scheduler = self.scheduler.lock().unwrap();
        for (name, collector) in hooks.collectors.iter() {
            scheduler.add(plugin_instance.instance_id, name, Arc::clone(&collector));
        }
        // Hook all the controllers into the dispatcher.
        let mut dispatcher = self.dispatcher.write().unwrap();
        for (namespace, controller) in hooks.controllers.iter() {
            info!("Plugin Hook: {} -> {}", namespace, desc);
            dispatcher.add(
                plugin_instance.instance_id,
                namespace.clone(),
                Arc::clone(&controller),
            )?;
        }
        plugin_instance.state = PluginState::Loaded;
        return Ok(());
    }

    /// Attempts to unload the plugin, this will fail if the plugin
    /// is already unloaded, not registered or has dependent plugins still loaded.
    pub fn unload(&mut self, desc: &PluginDescriptor) -> Result<()> {
        info!("Plugin Unload: {}", desc);
        let mut deps = Vec::new();
        if let Some(plugin_instance) = self.plugins.get_mut(desc) {
            if plugin_instance.state == PluginState::Unloaded {
                return log_error(PluginError::AlreadyUnloaded(desc.clone()));
            }
            // We cannot unload if plugins still depend on us.
            if plugin_instance.consumers.len() != 0 {
                return log_error(PluginError::InUseByMany(
                    desc.clone(),
                    plugin_instance.consumers.clone(),
                ));
            }

            let mut dispatcher = self.dispatcher.write().unwrap();
            let mut scheduler = self.scheduler.lock().unwrap();
            scheduler.remove_all(plugin_instance.instance_id);
            dispatcher.remove(plugin_instance.instance_id);
            plugin_instance.state = PluginState::Unloaded;
            deps.append(&mut plugin_instance.plugin.dependencies().clone());
        } else {
            return log_error(PluginError::NotFound(desc.clone()));
        }
        // Unregister all the consumer bindings.
        for dep_desc in deps.iter() {
            let dep_instance = self.plugins.get_mut(&dep_desc).unwrap();
            dep_instance.consumers.remove(desc);
        }
        Ok(())
    }

    /// Returns a list of all plugin descriptors registered with the system.
    pub fn plugins(&self) -> Vec<PluginDescriptor> {
        let mut plugins: Vec<PluginDescriptor> = self.plugins.keys().map(|k| k.clone()).collect();
        plugins.sort();
        plugins
    }

    /// Returns the state the plugin is currently in.
    pub fn state(&self, desc: &PluginDescriptor) -> Result<PluginState> {
        if let Some(plugin_instance) = self.plugins.get(desc) {
            Ok(plugin_instance.state.clone())
        } else {
            Err(PluginError::NotFound(desc.clone()).into())
        }
    }

    /// Returns the unique instance identifier for a particular descriptor.
    pub fn instance_id(&self, desc: &PluginDescriptor) -> Result<Uuid> {
        if let Some(plugin_instance) = self.plugins.get(desc) {
            Ok(plugin_instance.instance_id.clone())
        } else {
            Err(PluginError::NotFound(desc.clone()).into())
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            engine::hook::PluginHooks,
            model::{
                collector::DataCollector,
                controller::{ConnectionMode, DataController},
                model::DataModel,
            },
        },
        serde_json::{json, value::Value},
        tempfile::tempdir,
    };

    #[derive(Default)]
    pub struct TestCollector;
    impl DataCollector for TestCollector {
        fn collect(&self, _: Arc<DataModel>) -> Result<()> {
            Ok(())
        }
    }

    #[derive(Default)]
    pub struct TestController;
    impl DataController for TestController {
        fn query(&self, _: Arc<DataModel>, _: Value) -> Result<Value> {
            Ok(json!("foo"))
        }
    }

    plugin!(
        TestPluginOne,
        PluginHooks::new(
            collectors! {
                "TestCollector" => TestCollector::default(),
            },
            controllers! {
                "/foo/bar" => TestController::default(),
            }
        ),
        vec![PluginDescriptor::new("TestPluginTwo".to_string()),]
    );

    plugin!(
        TestPluginTwo,
        PluginHooks::new(
            collectors! {
                "TestCollector" => TestCollector::default(),
            },
            controllers! {
                "/foo/baz" => TestController::default(),
            }
        ),
        vec![]
    );

    plugin!(
        TestCycleOne,
        PluginHooks::new(collectors! {}, controllers! {}),
        vec![PluginDescriptor::new("TestCycleTwo".to_string()),]
    );

    plugin!(
        TestCycleTwo,
        PluginHooks::new(collectors![], controllers! {}),
        vec![PluginDescriptor::new("TestCycleOne".to_string()),]
    );

    fn create_manager() -> PluginManager {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let collector = Arc::new(Mutex::new(CollectorScheduler::new(Arc::clone(&model))));
        PluginManager::new(collector, dispatcher)
    }

    #[test]
    fn test_register() {
        let mut manager = create_manager();
        assert_eq!(manager.register(Box::new(TestPluginOne::new())).is_ok(), true);
        assert_eq!(manager.register(Box::new(TestPluginTwo::new())).is_ok(), true);
        assert_eq!(manager.plugins().len(), 2);
        assert_eq!(
            manager.plugins().contains(&PluginDescriptor::new("TestPluginOne".to_string())),
            true
        );
        assert_eq!(
            manager.plugins().contains(&PluginDescriptor::new("TestPluginTwo".to_string())),
            true
        );
    }

    #[test]
    fn test_unregister() {
        let mut manager = create_manager();
        let plugin = Box::new(TestPluginTwo::new());
        let desc = plugin.descriptor().clone();
        manager.register(plugin).expect("failed to register");
        assert_eq!(manager.unregister(&desc).is_ok(), true);
    }

    #[test]
    fn test_unregister_blocked_loaded() {
        let mut manager = create_manager();
        let plugin = Box::new(TestPluginTwo::new());
        let desc = plugin.descriptor().clone();
        manager.register_and_load(plugin).expect("failed to register");
        assert_eq!(manager.unregister(&desc).is_err(), true);
        manager.unload(&desc).expect("failed to unload");
        assert_eq!(manager.unregister(&desc).is_ok(), true);
    }

    #[test]
    fn test_load() {
        let mut manager = create_manager();
        let plugin = Box::new(TestPluginTwo::new());
        let desc = plugin.descriptor().clone();
        manager.register(plugin).expect("failed to register");
        assert_eq!(manager.load(&desc).is_ok(), true);
        assert_eq!(manager.state(&desc).unwrap(), PluginState::Loaded);
    }

    #[test]
    fn test_load_deps() {
        let mut manager = create_manager();
        let plugin_one = Box::new(TestPluginOne::new());
        let plugin_one_desc = plugin_one.descriptor().clone();
        manager.register(plugin_one).expect("failed to register");
        assert_eq!(manager.load(&plugin_one_desc).is_err(), true);
        let plugin_two = Box::new(TestPluginTwo::new());
        manager.register_and_load(plugin_two).expect("failed to load plugin two");
        assert_eq!(manager.load(&plugin_one_desc).is_ok(), true);
    }

    #[test]
    fn test_unload() {
        let mut manager = create_manager();
        let plugin = Box::new(TestPluginTwo::new());
        let desc = plugin.descriptor().clone();
        manager.register_and_load(plugin).expect("failed to load plugin two");
        assert_eq!(manager.unload(&desc).is_ok(), true);
        assert_eq!(manager.state(&desc).unwrap(), PluginState::Unloaded);
    }

    #[test]
    fn test_unload_blocked_dep() {
        let mut manager = create_manager();
        let plugin_one = Box::new(TestPluginOne::new());
        let plugin_one_desc = plugin_one.descriptor().clone();
        let plugin_two = Box::new(TestPluginTwo::new());
        let plugin_two_desc = plugin_two.descriptor().clone();
        manager.register_and_load(plugin_two).expect("failed to load plugin two");
        manager.register_and_load(plugin_one).expect("failed to load plugin one");
        assert_eq!(manager.unload(&plugin_two_desc).is_err(), true);
        assert_eq!(manager.unload(&plugin_one_desc).is_ok(), true);
        assert_eq!(manager.unload(&plugin_two_desc).is_ok(), true);
    }

    #[test]
    fn test_load_dep_cycle_blocked() {
        let mut manager = create_manager();
        let plugin_one = Box::new(TestCycleOne::new());
        let plugin_one_desc = plugin_one.descriptor().clone();
        let plugin_two = Box::new(TestCycleTwo::new());
        let plugin_two_desc = plugin_two.descriptor().clone();
        manager.register(plugin_one).expect("failed to register plugin one");
        manager.register(plugin_two).expect("failed to register plugin two");
        assert_eq!(manager.load(&plugin_one_desc).is_err(), true);
        assert_eq!(manager.load(&plugin_two_desc).is_err(), true);
    }

    #[test]
    fn test_dispatcher_hook() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let dispatcher = Arc::new(RwLock::new(ControllerDispatcher::new(Arc::clone(&model))));
        let collector = Arc::new(Mutex::new(CollectorScheduler::new(Arc::clone(&model))));
        let mut manager = PluginManager::new(collector, Arc::clone(&dispatcher));

        let plugin_one = Box::new(TestPluginOne::new());
        let plugin_one_desc = plugin_one.descriptor().clone();
        let plugin_two = Box::new(TestPluginTwo::new());
        let plugin_two_desc = plugin_two.descriptor().clone();
        manager.register_and_load(plugin_two).expect("failed to load plugin two");
        manager.register_and_load(plugin_one).expect("failed to load plugin one");

        assert_eq!(
            dispatcher
                .read()
                .unwrap()
                .query(ConnectionMode::Remote, "/api/foo/bar".to_string(), json!(""))
                .unwrap(),
            json!("foo")
        );
        assert_eq!(
            dispatcher
                .read()
                .unwrap()
                .query(ConnectionMode::Remote, "/api/foo/baz".to_string(), json!(""))
                .unwrap(),
            json!("foo")
        );

        manager.unload(&plugin_one_desc).expect("failed to unload plugin one");
        assert_eq!(
            dispatcher
                .read()
                .unwrap()
                .query(ConnectionMode::Remote, "/api/foo/bar".to_string(), json!(""))
                .is_err(),
            true
        );
        assert_eq!(
            dispatcher
                .read()
                .unwrap()
                .query(ConnectionMode::Remote, "/api/foo/baz".to_string(), json!(""))
                .unwrap(),
            json!("foo")
        );
        manager.unload(&plugin_two_desc).expect("failed to unload plugin two");
        assert_eq!(
            dispatcher
                .read()
                .unwrap()
                .query(ConnectionMode::Remote, "/api/foo/baz".to_string(), json!(""))
                .is_err(),
            true
        );
    }

    #[test]
    fn test_plugins_sorted() {
        let mut manager = create_manager();
        let plugin_one = Box::new(TestPluginOne::new());
        let plugin_one_desc = plugin_one.descriptor().clone();
        let plugin_two = Box::new(TestPluginTwo::new());
        let plugin_two_desc = plugin_two.descriptor().clone();
        manager.register_and_load(plugin_two).expect("failed to load plugin two");
        manager.register_and_load(plugin_one).expect("failed to load plugin one");
        let plugins = manager.plugins();
        assert_eq!(plugins[0], plugin_one_desc);
        assert_eq!(plugins[1], plugin_two_desc);
    }
}
