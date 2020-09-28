// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod shim;

#[cfg(target_arch = "wasm32")]
mod bindings {
    use wasm_bindgen::prelude::*;

    use {super::shim, serde_json5, std::collections::HashMap};

    macro_rules! format_js_err {
        ($msg:literal $(,)?) => {
            Err(JsValue::from_str($msg))
        };
        ($fmt:expr, $($arg:tt)*) => {
            Err(JsValue::from_str(&format!($fmt, $($arg)*).as_str()))
        };
    }

    macro_rules! bail_js_err {
        ($msg:literal $(,)?) => {
            return format_js_err!($msg);
        };
        ($fmt:expr, $($arg:tt)*) => {
            return format_js_err!($fmt, $($arg)*);
        };
    }

    /// Unique identifier to resources too expensive to pass between Rust/JS layer.
    pub type Handle = shim::Handle;

    /// Type of target content. This enum's values must be consistent with
    /// enum Source in fuchsia-mirror/src/diagnostics/lib/triage/src/config.rs,
    /// so they are listed explicitly and must never be changed. They do not
    /// have to be sequential.
    #[wasm_bindgen]
    #[derive(Debug, Copy, Clone)]
    pub enum Source {
        Inspect = 0,
        Klog = 1,
        Syslog = 2,
        Bootlog = 3,
        Annotations = 4,
    }

    /// Object to manage lifetime of objects needed for Triage analysis.
    #[wasm_bindgen]
    pub struct TriageManager {
        #[wasm_bindgen(skip)]
        pub shim: shim::TriageManager,
    }

    #[wasm_bindgen]
    impl TriageManager {
        #[wasm_bindgen(constructor)]
        pub fn new() -> TriageManager {
            TriageManager { shim: shim::TriageManager::new() }
        }

        /// Attempt to build new Context using map of configs.
        /// Returns a Handle that can be passed to `TriageManager::analyze` method.
        ///
        /// # Arguments
        ///
        /// * `configs` - JS map of configs objects to
        ///               forward to triage library.
        #[wasm_bindgen]
        pub fn build_context(&mut self, configs: JsValue) -> Result<Handle, JsValue> {
            let serialized_configs = configs.as_string();
            if serialized_configs.is_none() {
                bail_js_err!("configs param is not String.");
            }

            match serde_json5::from_str::<HashMap<String, String>>(
                &serialized_configs.unwrap().as_str(),
            ) {
                Err(err) => format_js_err!("Failed to deserialize configs: {}", err),
                Ok(configs) => match self.shim.build_context(configs) {
                    Err(err) => format_js_err!("Failed to parse configs: {}", err),
                    Ok(id) => Ok(id),
                },
            }
        }

        /// Attempt to build new Target.
        /// Returns a Handle that can be passed to `TriageManager::analyze` method.
        ///
        /// # Arguments
        ///
        /// * `source` - Type for target (e.g. Inspect for "inspect.json").
        /// * `name` - Name of target (e.g. filename).
        /// * `content` - Content of target file.
        #[wasm_bindgen]
        pub fn build_target(
            &mut self,
            source: Source,
            name: &str,
            content: &str,
        ) -> Result<Handle, JsValue> {
            match self.shim.build_target(name, source as u32, content) {
                Ok(id) => Ok(id),
                Err(err) => match source {
                    Source::Inspect => format_js_err!("Failed to parse Inspect tree: {}", err),
                    Source::Syslog => format_js_err!("Failed to parse Syslog file: {}", err),
                    Source::Klog => format_js_err!("Failed to parse Klog file: {}", err),
                    Source::Bootlog => format_js_err!("Failed to parse Bootlog file: {}", err),
                    Source::Annotations => format_js_err!("Failed to parse Annotations: {}", err),
                },
            }
        }

        /// Analyze all DiagnosticData against loaded configs and
        /// generate corresponding ActionResults.
        /// Returns JSON-serialized value of triage library's `analyze` function's return value.
        ///
        /// # Arguments
        ///
        /// * `targets` - Handles for targets.
        /// * `context` - Handle for context.
        #[wasm_bindgen]
        pub fn analyze(&mut self, targets: &[Handle], context: Handle) -> Result<JsValue, JsValue> {
            match self.shim.analyze(&targets, context) {
                Ok(results) => Ok(JsValue::from_str(&results)),
                Err(err) => format_js_err!("Failed to run analysis: {}", err),
            }
        }
    }
}
