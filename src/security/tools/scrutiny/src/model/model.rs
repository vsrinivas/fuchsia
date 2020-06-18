// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, serde::Serialize, std::sync::RwLock};

pub struct DataModel {
    components: RwLock<Vec<Component>>,
    manifests: RwLock<Vec<Manifest>>,
    routes: RwLock<Vec<Route>>,
}

impl DataModel {
    // TODO(benwright) - Implement once third_party is approved.
    pub fn connect() -> Result<Self> {
        Ok(Self {
            components: RwLock::new(Vec::new()),
            manifests: RwLock::new(Vec::new()),
            routes: RwLock::new(Vec::new()),
        })
    }

    pub fn components(&self) -> &RwLock<Vec<Component>> {
        &self.components
    }

    pub fn manifests(&self) -> &RwLock<Vec<Manifest>> {
        &self.manifests
    }

    pub fn routes(&self) -> &RwLock<Vec<Route>> {
        &self.routes
    }
}

/// Defines a component. Each component has a unique id which is used to link
/// it in the Route table. Each component also has a url and a version. This
/// structure is intended to be lightweight and general purpose if you need to
/// append additional information about a component make another table and
/// index it on the `component.id`.
#[derive(Serialize)]
pub struct Component {
    pub id: i32,
    pub url: String,
    pub version: i32,
    pub inferred: bool,
}

/// A component instance is a specific instantiation of a component. These
/// may run in a particular realm with certain restrictions.
pub struct ComponentInstance {
    pub id: i32,
    pub moniker: String,
    pub component_id: i32,
}

/// Defines a component manifest. The `component_id` maps 1:1 to
/// `component.id` indexes. This is stored in a different table as most queries
/// don't need the raw manifest.
#[derive(Serialize)]
pub struct Manifest {
    pub component_id: i32,
    pub manifest: String,
    // uses should not be persisted to backing key-value store, it is primarily
    // used to build routes.
    pub uses: Vec<String>,
}

// TODO(benwright) - Add support for "first class" capabilities such as runners,
// resolvers and events.
/// Defines a link between two components. The `src_id` is the `component_instance.id`
/// of the component giving a service or directory to the `dst_id`. The
/// `protocol_id` refers to the Protocol with this link.
#[derive(Serialize)]
pub struct Route {
    pub id: i32,
    pub src_id: i32,
    pub dst_id: i32,
    pub protocol_id: i32,
}

/// Defines either a FIDL or Directory protocol with some interface name such
/// as fuchshia.foo.Bar and an optional path such as "/dev".
pub struct Protocol {
    pub id: i32,
    pub interface: String,
    pub path: String,
}
