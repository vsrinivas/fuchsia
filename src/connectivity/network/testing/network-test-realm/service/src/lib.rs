// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _, Result};
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fdecl;

/// Name of the collection that contains the hermetic network realm.
pub const HERMETIC_NETWORK_COLLECTION_NAME: &'static str = "enclosed-network";

/// Name of the realm that contains the hermetic network components.
pub const HERMETIC_NETWORK_REALM_NAME: &'static str = "hermetic-network";

/// Returns true if the hermetic network realm exists.
///
/// The provided `realm_proxy` should correspond to the Network Test Realm
/// controller component.
///
/// # Errors
///
/// An error will be returned if the `realm_proxy` encounters an error while
/// attempting to list the children of the Network Test Realm.
pub async fn has_hermetic_network_realm(realm_proxy: &fcomponent::RealmProxy) -> Result<bool> {
    let (iterator_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let mut collection_ref =
        fdecl::CollectionRef { name: HERMETIC_NETWORK_COLLECTION_NAME.to_string() };
    let list_children_result = realm_proxy
        .list_children(&mut collection_ref, server_end)
        .await
        .context("failed to list_children")?;

    match list_children_result {
        Ok(()) => {
            let children =
                iterator_proxy.next().await.context("failed to iterate over children")?;

            let expected_child = create_hermetic_network_relam_child_ref();

            Ok(children.iter().find(|&child| *child == expected_child).is_some())
        }
        Err(error) => match error {
            // Variants that may be returned by the `ListChildren` method.
            // `CollectionNotFound` means that the hermetic network realm does
            // not exist. All other errors are propagated.
            fcomponent::Error::CollectionNotFound => Ok(false),
            fcomponent::Error::AccessDenied
            | fcomponent::Error::InstanceDied
            | fcomponent::Error::InvalidArguments
            // Variants that are not returned by the `ListChildren` method.
            | fcomponent::Error::InstanceAlreadyExists
            | fcomponent::Error::InstanceCannotResolve
            | fcomponent::Error::InstanceCannotStart
            | fcomponent::Error::InstanceNotFound
            | fcomponent::Error::Internal
            | fcomponent::Error::ResourceNotFound
            | fcomponent::Error::ResourceUnavailable
            | fcomponent::Error::Unsupported => {
                Err(anyhow!("failed to list children: {:?}", error))
            }
        },
    }
}

/// Returns a `fdecl::ChildRef` that corresponds to the hermetic network realm.
pub fn create_hermetic_network_relam_child_ref() -> fdecl::ChildRef {
    fdecl::ChildRef {
        name: HERMETIC_NETWORK_REALM_NAME.to_string(),
        collection: Some(HERMETIC_NETWORK_COLLECTION_NAME.to_string()),
    }
}
