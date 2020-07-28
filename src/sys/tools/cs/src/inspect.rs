// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_inspect_deprecated::Object;
use fidl_fuchsia_inspect_deprecated::{InspectMarker, MetricValue, PropertyValue};
use fuchsia_zircon as zx;
use std::path::Path;

type TraversalResult = Result<(), Error>;

pub struct InspectObject {
    pub inspect_object: Object,
    pub child_inspect_objects: Vec<InspectObject>,
}

impl InspectObject {
    pub fn create(
        exclude_objects: &Vec<String>,
        client_channel: zx::Channel,
    ) -> Result<InspectObject, Error> {
        let mut inspect =
            fidl_fuchsia_inspect_deprecated::InspectSynchronousProxy::new(client_channel);
        let inspect_object = inspect.read_data(zx::Time::INFINITE)?;

        let children = inspect.list_children(zx::Time::INFINITE)?;
        let mut child_inspect_objects = Vec::with_capacity(children.len());
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
        Ok(InspectObject { inspect_object, child_inspect_objects })
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

pub fn visit_system_objects(
    component_path: &Path,
    exclude_objects: &Vec<String>,
) -> TraversalResult {
    let channel_path = component_path
        .join("system_objects")
        .join(<InspectMarker as fidl::endpoints::ServiceMarker>::NAME);
    let inspect_object = generate_inspect_object_tree(&channel_path, &exclude_objects)?;
    visit_inspect_object(1, &inspect_object);
    Ok(())
}

fn visit_inspect_object(depth: usize, inspect_object: &InspectObject) {
    let indent = " ".repeat(depth);
    println!("{}{}", indent, inspect_object.inspect_object.name);
    for metric in &inspect_object.inspect_object.metrics {
        println!(
            "{} {}: {}",
            indent,
            metric.key,
            match &metric.value {
                MetricValue::IntValue(v) => format!("{}", v),
                MetricValue::UintValue(v) => format!("{}", v),
                MetricValue::DoubleValue(v) => format!("{}", v),
            },
        );
    }
    for property in &inspect_object.inspect_object.properties {
        println!(
            "{} {}: {}",
            indent,
            property.key,
            match &property.value {
                PropertyValue::Str(s) => s.clone(),
                PropertyValue::Bytes(_) => String::from("<binary>"),
            },
        );
    }
    for child in &inspect_object.child_inspect_objects {
        visit_inspect_object(depth + 1, child);
    }
}
