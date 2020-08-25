// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::hook::PluginHooks,
    serde::{Deserialize, Serialize},
    std::fmt,
};

/// Core identifying information about the plugin.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize, PartialOrd, Ord)]
pub struct PluginDescriptor {
    // A unique name for the plugin.
    name: String,
}

impl PluginDescriptor {
    pub fn new(name: impl Into<String>) -> Self {
        Self { name: name.into() }
    }
}

impl fmt::Display for PluginDescriptor {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.name)
    }
}

/// A ScrutinyPlugin is defined as a collection of data controllers
/// and data collectors that can be dynamically loaded and unloaded.
pub trait Plugin: Send + Sync {
    /// Returns the identifying plugin information.
    fn descriptor(&self) -> &PluginDescriptor;
    /// Other plugins which must be loaded for this plugin to operate correctly.
    fn dependencies(&self) -> &Vec<PluginDescriptor>;
    /// Returns all the `DataController` and `DataCollectors` this plugin
    /// would like to install.
    fn hooks(&mut self) -> &PluginHooks;
}

/// Utility macro to automatically create the plugin boilerplate. This creates
/// a general template that is usable for most plugins. Plugins that need to
/// do custom logic when hooking should simply implement the trait directly.
#[macro_export]
macro_rules! plugin {
    ($name:ident, $hooks:expr, $deps:expr) => {
        pub struct $name {
            desc: PluginDescriptor,
            hooks: PluginHooks,
            deps: Vec<PluginDescriptor>,
        }
        impl $name {
            pub fn new() -> Self {
                Self {
                    desc: PluginDescriptor::new(stringify!($name).to_string()),
                    hooks: $hooks,
                    deps: $deps,
                }
            }
        }
        impl Plugin for $name {
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
    };
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::collector::DataCollector,
        crate::model::controller::DataController,
        crate::model::model::DataModel,
        anyhow::Result,
        serde_json::{json, value::Value},
        std::sync::Arc,
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
        TestPlugin,
        PluginHooks::new(
            collectors! {
                "TestCollector" => TestCollector::default(),
            },
            controllers! {
                "/foo/bar" => TestController::default(),
            }
        ),
        vec![PluginDescriptor::new("FooPlugin"), PluginDescriptor::new("BarPlugin")]
    );

    fn test_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
    }

    #[test]
    fn test_plugin_macro() {
        let mut plugin = TestPlugin::new();
        assert_eq!(*plugin.descriptor(), PluginDescriptor::new("TestPlugin"));
        assert_eq!(
            *plugin.dependencies(),
            vec![PluginDescriptor::new("FooPlugin"), PluginDescriptor::new("BarPlugin")]
        );
        let hooks = plugin.hooks();
        assert_eq!(hooks.collectors.len(), 1);
        assert_eq!(hooks.controllers.len(), 1);
        let model = test_model();
        assert_eq!(hooks.controllers.contains_key("/api/foo/bar"), true);
        assert_eq!(
            hooks.controllers.get("/api/foo/bar").unwrap().query(model, json!("")).unwrap(),
            json!("foo")
        );
    }
}
