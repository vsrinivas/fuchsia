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
    pub collectors: Vec<Arc<dyn DataCollector>>,
    pub controllers: HashMap<String, Arc<dyn DataController>>,
}

impl PluginHooks {
    pub fn new(
        collectors: Vec<Arc<dyn DataCollector>>,
        controllers: HashMap<String, Arc<dyn DataController>>,
    ) -> Self {
        Self { collectors: collectors, controllers: controllers }
    }
}

// Utility macro to generate controller hook mappings from a namespace => constructor
// mapping. It automatically fixes the string type and sets the correct arc, rwlock
// on the controllers provided.
#[macro_use]
macro_rules! controller_hooks {
    ($($ns:expr => $ctrl:expr,)+) => {controller_hooks!($($ns => $ctrl),+)};
    ($($ns:expr => $ctrl:expr),*) => {{
            let mut _hooks: ::std::collections::HashMap<String,
            std::sync::Arc<dyn crate::model::controller::DataController>> = ::std::collections::HashMap::new();
            $(
                _hooks.insert(String::from($ns), Arc::new($ctrl));
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
        let hooks = controller_hooks! {
            "/foo/bar" => FakeController::default(),
            "/foo/baz" => FakeController::default(),
        };
        assert_eq!(hooks.contains_key("/foo/bar"), true);
        assert_eq!(hooks.contains_key("/foo/baz"), true);
        assert_eq!(hooks.len(), 2);
    }
}
