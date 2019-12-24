// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_inspect_deprecated::Object;
use fuchsia_zircon as zx;
use std::path::Path;

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
