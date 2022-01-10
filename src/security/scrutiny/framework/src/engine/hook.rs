// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::collector::DataCollector, crate::model::controller::DataController,
    std::collections::HashMap, std::sync::Arc,
};

/// `PluginHooks` holds all the collectors and controllers that a plugin needs
/// to operate correctly. These are hooked into the `ControllerDispatcher` and
/// `CollectorScheduler` when `load` is called and removed when `unload` is
/// called.
#[derive(Clone)]
pub struct PluginHooks {
    pub collectors: HashMap<String, Arc<dyn DataCollector>>,
    pub controllers: HashMap<String, Arc<dyn DataController>>,
}

impl PluginHooks {
    pub fn new(
        collectors: HashMap<String, Arc<dyn DataCollector>>,
        controllers: HashMap<String, Arc<dyn DataController>>,
    ) -> Self {
        Self { collectors: collectors, controllers: controllers }
    }
}

// Utility macro to give unique names to each collector for a given plugin.
// This additional naming information is used to make individual controllers
// human readable.
#[macro_export]
macro_rules! collectors {
    ($($ns:expr => $ctrl:expr,)+) => {collectors!($($ns => $ctrl),+)};
    ($($ns:expr => $ctrl:expr),*) => {{
            let mut _collectors: ::std::collections::HashMap<String,
            std::sync::Arc<dyn DataCollector>> = ::std::collections::HashMap::new();
            $(
                _collectors.insert(String::from($ns), Arc::new($ctrl));
            )*
            _collectors
        }}
}

// Utility macro to generate controller hook mappings from a namespace => constructor
// mapping. It automatically fixes the string type and sets the correct arc, rwlock
// on the controllers provided.
#[macro_export]
macro_rules! controllers {
    ($($ns:expr => $ctrl:expr,)+) => {controllers!($($ns => $ctrl),+)};
    ($($ns:expr => $ctrl:expr),*) => {{
            let mut _hooks: ::std::collections::HashMap<String,
            std::sync::Arc<dyn DataController>> = ::std::collections::HashMap::new();
            $(
                if $ns.starts_with("/") {
                    _hooks.insert(format!("/api{}", $ns), Arc::new($ctrl));
                } else {
                    _hooks.insert(format!("/api/{}", $ns), Arc::new($ctrl));
                }
            )*
            _hooks
        }}
}

#[cfg(test)]
mod tests {
    use {
        crate::model::controller::DataController,
        crate::model::model::DataModel,
        anyhow::Result,
        serde_json::{json, value::Value},
        std::sync::Arc,
    };

    #[derive(Default)]
    struct FakeController {}

    impl DataController for FakeController {
        fn query(&self, _: Arc<DataModel>, _: Value) -> Result<Value> {
            Ok(json!(null))
        }
    }

    #[test]
    fn test_controller_hooks() {
        let hooks = controllers! {
            "/foo/bar" => FakeController::default(),
            "foo/baz" => FakeController::default(),
        };
        assert_eq!(hooks.contains_key("/api/foo/bar"), true);
        assert_eq!(hooks.contains_key("/api/foo/baz"), true);
        assert_eq!(hooks.len(), 2);
    }
}
