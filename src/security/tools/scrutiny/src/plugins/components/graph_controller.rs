// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate:: {
        model::controller::DataController,
        model::model::DataModel,
    },
    anyhow::Result,
    serde_json::value::Value,
    std::sync::Arc,
};  

#[derive(Default)]
pub struct ComponentGraphController {}

impl DataController for ComponentGraphController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {

        let components = model.components().read().unwrap();
        let mut comp_vec = Vec::new();
        for comp in components.iter() {
            comp_vec.push(comp);
        }

        Ok(serde_json::to_value(comp_vec)?)
    }
}

#[derive(Default)]
pub struct RouteGraphController {}

impl DataController for RouteGraphController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {

        let routes = model.routes().read().unwrap();
        let mut route_vec = Vec::new();
        for route in routes.iter() {
            route_vec.push(route);
        }

        Ok(serde_json::to_value(route_vec)?)
    }
}

#[derive(Default)]
pub struct ManifestGraphController {}

impl DataController for ManifestGraphController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {

        let manifests = model.manifests().read().unwrap();
        let mut mani_vec = Vec::new();
        for manifest in manifests.iter() {
            mani_vec.push(manifest);
        }

        Ok(serde_json::to_value(mani_vec)?)
    }
}