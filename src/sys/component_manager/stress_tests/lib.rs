// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::endpoints::Proxy,
    fidl_test_componentmanager_stresstests as fstresstests,
    fuchsia_component::client,
};
pub struct Child {
    pub instance: client::ScopedInstance,
    pub realm: fstresstests::ChildRealmProxy,
}

pub async fn create_child(collection: &str, url: &str) -> Result<Child, Error> {
    let instance = client::ScopedInstance::new(collection.to_string(), url.to_string())
        .await
        .context(format_err!("Cannot create child for '{}:{}'", collection, url))?;
    let realm = instance
        .connect_to_protocol_at_exposed_dir::<fstresstests::ChildRealmMarker>()
        .await
        .context(format_err!(
            "Cannot connect to child realm service for '{}'",
            instance.child_name()
        ))?;
    Ok(Child { instance, realm })
}

pub async fn stop_child(child: Child) -> Result<(), Error> {
    child
        .realm
        .stop()
        .context(format_err!("Error calling stop for '{}'", child.instance.child_name()))?;
    child.realm.on_closed().await.context(format_err!(
        "Error waiting for child to stop '{}'",
        child.instance.child_name()
    ))?;
    Ok(())
}
