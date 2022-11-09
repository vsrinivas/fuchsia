// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Zbi,
    anyhow::Result,
    scrutiny::{model::controller::DataController, model::model::DataModel},
    scrutiny_utils::usage::UsageBuilder,
    serde_json::{self, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct BootfsPathsController {}

impl DataController for BootfsPathsController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        if let Ok(zbi) = model.get::<Zbi>() {
            let mut paths = zbi.bootfs.keys().cloned().collect::<Vec<String>>();
            paths.sort();
            Ok(serde_json::to_value(paths)?)
        } else {
            let empty: Vec<String> = vec![];
            Ok(serde_json::to_value(empty)?)
        }
    }
    fn description(&self) -> String {
        "Returns all the files in bootfs.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("zbi.bootfs - Lists all the BootFS files found in the ZBI.")
            .summary("zbi.bootfs")
            .description(
                "Lists all the BootFS ZBI files found in the ZBI.\
            More specifically it is looking at the ZBI found in the \
            fuchsia-pkg://fuchsia.com/update package.",
            )
            .build()
    }
}

#[derive(Default)]
pub struct ZbiCmdlineController {}

impl DataController for ZbiCmdlineController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        if let Ok(zbi) = model.get::<Zbi>() {
            Ok(serde_json::to_value(zbi.cmdline.clone())?)
        } else {
            Ok(serde_json::to_value("")?)
        }
    }
    fn description(&self) -> String {
        "Returns the zbi cmdline section as a string.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("zbi.cmdline - Lists the command line params set in the ZBI.")
            .summary("zbi.cmdline")
            .description(
                "Lists all the command line parameters set in the ZBI. \
            More specifically it is looking at the ZBI found in the \
            fuchsia-pkg://fuchsia.com/update package.",
            )
            .build()
    }
}

#[derive(Default)]
pub struct ZbiSectionsController {}

impl DataController for ZbiSectionsController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        if let Ok(zbi) = model.get::<Zbi>() {
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
    fn description(&self) -> String {
        "Returns all the typed sections found in the zbi.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("zbi.sections - Lists the section types set in the ZBI.")
            .summary("zbi.sections")
            .description(
                "Lists all the unique section types set in the ZBI. \
            More specifically it is looking at the ZBI found in the \
            fuchsia-pkg://fuchsia.com/update package.",
            )
            .build()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, scrutiny_testing::fake::*, serde_json::json, std::collections::HashMap};

    #[test]
    fn bootfs_returns_files() {
        let model = fake_data_model();
        let mut zbi = Zbi { sections: vec![], bootfs: HashMap::new(), cmdline: "".to_string() };
        zbi.bootfs.insert("foo".to_string(), vec![]);
        model.set(zbi).unwrap();
        let controller = BootfsPathsController::default();
        let bootfs: Vec<String> =
            serde_json::from_value(controller.query(model, json!("")).unwrap()).unwrap();
        assert_eq!(bootfs.len(), 1);
        assert_eq!(bootfs[0], "foo".to_string());
    }

    #[test]
    fn zbi_cmdline() {
        let model = fake_data_model();
        let zbi = Zbi { sections: vec![], bootfs: HashMap::new(), cmdline: "foo".to_string() };
        model.set(zbi).unwrap();
        let controller = ZbiCmdlineController::default();
        let cmdline: String =
            serde_json::from_value(controller.query(model, json!("")).unwrap()).unwrap();
        assert_eq!(cmdline, "foo".to_string());
    }
}
