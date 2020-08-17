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
pub struct BootfsPathsController {}

impl DataController for BootfsPathsController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        if let Some(zbi) = &*model.zbi().read().unwrap() {
            let mut paths = zbi.bootfs.keys().cloned().collect::<Vec<String>>();
            paths.sort();
            Ok(serde_json::to_value(paths)?)
        } else {
            let empty: Vec<String> = vec![];
            Ok(serde_json::to_value(empty)?)
        }
    }
}

#[derive(Default)]
pub struct ZbiCmdlineController {}

impl DataController for ZbiCmdlineController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        if let Some(zbi) = &*model.zbi().read().unwrap() {
            Ok(serde_json::to_value(zbi.cmdline.clone())?)
        } else {
            Ok(serde_json::to_value("")?)
        }
    }
}

#[derive(Default)]
pub struct ZbiSectionsController {}

impl DataController for ZbiSectionsController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        if let Some(zbi) = &*model.zbi().read().unwrap() {
            let mut sections = vec![];
            for section in zbi.sections.iter() {
                sections.push(section.section_type.clone());
            }
            Ok(serde_json::to_value(sections)?)
        } else {
            let empty: Vec<String> = vec![];
            Ok(serde_json::to_value(empty)?)
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, scrutiny::model::model::*, serde_json::json, std::collections::HashMap,
        tempfile::tempdir,
    };

    #[test]
    fn bootfs_returns_files() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let mut zbi = Zbi { sections: vec![], bootfs: HashMap::new(), cmdline: "".to_string() };
        zbi.bootfs.insert("foo".to_string(), vec![]);
        *model.zbi().write().unwrap() = Some(zbi);
        let controller = BootfsPathsController::default();
        let bootfs: Vec<String> =
            serde_json::from_value(controller.query(model, json!("")).unwrap()).unwrap();
        assert_eq!(bootfs.len(), 1);
        assert_eq!(bootfs[0], "foo".to_string());
    }

    #[test]
    fn zbi_cmdline() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let zbi = Zbi { sections: vec![], bootfs: HashMap::new(), cmdline: "foo".to_string() };
        *model.zbi().write().unwrap() = Some(zbi);
        let controller = ZbiCmdlineController::default();
        let cmdline: String =
            serde_json::from_value(controller.query(model, json!("")).unwrap()).unwrap();
        assert_eq!(cmdline, "foo".to_string());
    }
}
