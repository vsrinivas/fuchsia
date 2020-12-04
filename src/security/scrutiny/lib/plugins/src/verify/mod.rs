// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod controller;

use {
    crate::verify::controller::boot_options::ZbiCmdlineVerifyController, scrutiny::prelude::*,
    std::sync::Arc,
};

plugin!(
    VerifyPlugin,
    PluginHooks::new(
        collectors! {},
        controllers! {
            "/verify/zbi_cmdline" => ZbiCmdlineVerifyController::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::core::collection::Zbi,
        anyhow::Result,
        scrutiny_utils::zbi::ZbiSection,
        serde_json::{json, value::Value},
        std::collections::HashMap,
        tempfile::tempdir,
    };

    fn data_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
    }

    fn zbi() -> Zbi {
        let bootfs: HashMap<String, Vec<u8>> = HashMap::default();
        let sections: Vec<ZbiSection> = Vec::default();
        return Zbi { sections: sections, bootfs: bootfs, cmdline: "".to_string() };
    }

    #[test]
    fn test_zbi_cmdline_verify_accepts() {
        let model = data_model();
        let zbi = Zbi { cmdline: "{kernel.enable-debugging-syscalls=false}".to_string(), ..zbi() };
        model.set(zbi).unwrap();
        let verify = ZbiCmdlineVerifyController::default();
        let response: Result<Value> = verify.query(model.clone(), json!("{}"));
        assert!(response.is_ok());
    }

    #[test]
    fn test_zbi_cmdline_verify_rejects() {
        let model = data_model();
        let zbi = Zbi { cmdline: "{kernel.enable-debugging-syscalls=true}".to_string(), ..zbi() };
        model.set(zbi).unwrap();
        let verify = ZbiCmdlineVerifyController::default();
        let response: Result<Value> = verify.query(model.clone(), json!("{}"));
        assert!(response.is_err());
    }
}
