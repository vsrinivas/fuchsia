// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client::{connect_to_protocol, connect_to_protocol_at_path},
};

pub struct Stressor {
    realm_explorer: fsys::RealmExplorerProxy,
    lifecycle_controller: fsys::LifecycleControllerProxy,
}

impl Stressor {
    pub fn from_namespace() -> Self {
        let realm_explorer = connect_to_protocol::<fsys::RealmExplorerMarker>().unwrap();
        let lifecycle_controller = connect_to_protocol_at_path::<fsys::LifecycleControllerMarker>(
            "/hub/debug/fuchsia.sys2.LifecycleController",
        )
        .unwrap();
        Self { realm_explorer, lifecycle_controller }
    }

    pub async fn get_instances_in_realm(&self) -> Vec<String> {
        let iterator = self.realm_explorer.get_all_instance_infos().await.unwrap().unwrap();
        let iterator = iterator.into_proxy().unwrap();

        let mut instances = vec![];

        loop {
            let mut slice = iterator.next().await.unwrap();
            if slice.is_empty() {
                break;
            }
            instances.append(&mut slice);
        }

        instances.into_iter().map(|i| i.moniker).collect()
    }

    pub async fn create_child(
        &self,
        parent_moniker: &str,
        collection: String,
        child_name: String,
        url: String,
    ) -> Result<()> {
        let mut collection_ref = fdecl::CollectionRef { name: collection.clone() };
        let decl = fdecl::Child {
            name: Some(child_name.clone()),
            url: Some(url),
            startup: Some(fdecl::StartupMode::Lazy),
            ..fdecl::Child::EMPTY
        };
        self.lifecycle_controller
            .create_child(
                parent_moniker,
                &mut collection_ref,
                decl,
                fcomponent::CreateChildArgs::EMPTY,
            )
            .await
            .unwrap()
            .map_err(|e| {
                format_err!(
                    "Could not create child (parent: {})(child: {}): {:?}",
                    parent_moniker,
                    child_name,
                    e
                )
            })?;

        let child_moniker = format!("{}/{}:{}", parent_moniker, collection, child_name);
        self.lifecycle_controller
            .start(&child_moniker)
            .await
            .unwrap()
            .map_err(|e| format_err!("Could not start {}: {:?}", child_moniker, e))?;

        Ok(())
    }

    pub async fn destroy_child(
        &self,
        parent_moniker: &str,
        collection: String,
        child_name: String,
    ) -> Result<()> {
        let mut child = fdecl::ChildRef { name: child_name.clone(), collection: Some(collection) };
        self.lifecycle_controller.destroy_child(parent_moniker, &mut child).await.unwrap().map_err(
            |e| {
                format_err!(
                    "Could not destroy child (parent: {})(child: {}): {:?}",
                    parent_moniker,
                    child_name,
                    e
                )
            },
        )
    }
}
