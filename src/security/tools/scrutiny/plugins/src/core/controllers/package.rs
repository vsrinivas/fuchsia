// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    scrutiny::{model::controller::DataController, model::model::DataModel},
    serde_json::{self, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct PackagesGraphController {}

impl DataController for PackagesGraphController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        let mut packages = model.packages().read().unwrap().clone();
        packages.sort_by(|a, b| a.url.partial_cmp(&b.url).unwrap());
        Ok(serde_json::to_value(packages)?)
    }
    fn description(&self) -> String {
        "Returns all Fuchsia packages and their file index.".to_string()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, scrutiny::model::model::*, serde_json::json, std::collections::HashMap,
        tempfile::tempdir,
    };

    #[test]
    fn packages_test() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let packages = PackagesGraphController::default();
        model.packages().write().unwrap().push(Package {
            url: "foo".to_string(),
            merkle: "bar".to_string(),
            contents: HashMap::new(),
        });
        let value = packages.query(model, json!("")).unwrap();
        let response: Vec<Package> = serde_json::from_value(value).unwrap();
        assert_eq!(response.len(), 1);
    }
}
