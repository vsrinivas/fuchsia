// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_inspect::{Metric, Property};
use fuchsia_zircon as zx;
use std::path::Path;

pub struct InspectObject {
    pub name: String,
    pub metrics: Vec<Metric>,
    pub properties: Vec<Property>,
    pub child_inspect_objects: Vec<InspectObject>,
}

impl InspectObject {
    pub fn create(
        exclude_objects: &Vec<String>,
        client_channel: zx::Channel,
    ) -> Result<InspectObject, Error> {
        let mut inspect = fidl_fuchsia_inspect::InspectSynchronousProxy::new(client_channel);
        let obj = inspect.read_data(zx::Time::INFINITE)?;

        let mut inspect_metrics = Vec::new();
        if let Some(metrics) = obj.metrics {
            for metric in metrics {
                inspect_metrics.push(metric);
            }
        }
        let mut inspect_properties = Vec::new();
        if let Some(properties) = obj.properties {
            for property in properties {
                inspect_properties.push(property);
            }
        }
        let mut child_inspect_objects = Vec::new();
        if let Some(children) = inspect.list_children(zx::Time::INFINITE)? {
            for child in &children {
                if exclude_objects.contains(&child) {
                    continue;
                }
                let (client, service) = zx::Channel::create()?;
                inspect.open_child(
                    child,
                    fidl::endpoints::ServerEnd::new(service),
                    zx::Time::INFINITE,
                )?;
                child_inspect_objects.push(InspectObject::create(exclude_objects, client)?);
            }
        }
        Ok(InspectObject {
            name: obj.name,
            metrics: inspect_metrics,
            properties: inspect_properties,
            child_inspect_objects: child_inspect_objects,
        })
    }
}

/// Given a path within the Hub to a component, this function generates an
/// Inspect object tree consistenting of properties that the component broadcasts
/// about itself.
pub fn generate_inspect_object_tree(
    path: &Path,
    exclude_objects: &Vec<String>,
) -> Result<InspectObject, Error> {
    let (client, service) = zx::Channel::create()?;
    fdio::service_connect(path.to_string_lossy().as_ref(), service)?;
    Ok(InspectObject::create(exclude_objects, client)?)
}
