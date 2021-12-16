// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _, Result};
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fdecl;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_net_interfaces_ext as fnet_interfaces_ext;
use std::collections::HashMap;

/// Name of the collection that contains the hermetic network realm.
pub const HERMETIC_NETWORK_COLLECTION_NAME: &'static str = "enclosed-network";

/// Name of the realm that contains the hermetic network components.
pub const HERMETIC_NETWORK_REALM_NAME: &'static str = "hermetic-network";

/// Name of the collection that contains the test stub.
pub const STUB_COLLECTION_NAME: &'static str = "stubs";

/// Name of the component that corresponds to the test stub.
pub const STUB_COMPONENT_NAME: &'static str = "test-stub";

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
    let child_ref = create_hermetic_network_realm_child_ref();
    has_running_child(
        fdecl::CollectionRef { name: HERMETIC_NETWORK_COLLECTION_NAME.to_string() },
        &child_ref,
        realm_proxy,
    )
    .await
}

/// Returns true if the hermetic-network realm contains a stub.
///
/// The provided `realm_proxy` should correspond to the hermetic-network realm.
///
/// # Errors
///
/// An error will be returned if the `realm_proxy` encounters an error while
/// attempting to list the children of the hermetic-network realm.
pub async fn has_stub(realm_proxy: &fcomponent::RealmProxy) -> Result<bool> {
    let child_ref = create_stub_child_ref();
    has_running_child(
        fdecl::CollectionRef { name: STUB_COLLECTION_NAME.to_string() },
        &child_ref,
        realm_proxy,
    )
    .await
}

/// Returns true if the `realm_proxy` contains the `expected_child_ref` within
/// the provided `collection_ref`.
async fn has_running_child(
    mut collection_ref: fdecl::CollectionRef,
    expected_child_ref: &fdecl::ChildRef,
    realm_proxy: &fcomponent::RealmProxy,
) -> Result<bool> {
    let (iterator_proxy, server_end) = fidl::endpoints::create_proxy().unwrap();
    let list_children_result = realm_proxy
        .list_children(&mut collection_ref, server_end)
        .await
        .context("failed to list_children")?;

    match list_children_result {
        Ok(()) => {
            let children =
                iterator_proxy.next().await.context("failed to iterate over children")?;

            Ok(children.iter().any(|child| child == expected_child_ref))
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
pub fn create_hermetic_network_realm_child_ref() -> fdecl::ChildRef {
    fdecl::ChildRef {
        name: HERMETIC_NETWORK_REALM_NAME.to_string(),
        collection: Some(HERMETIC_NETWORK_COLLECTION_NAME.to_string()),
    }
}

/// Returns a `fdecl::ChildRef` that corresponds to the test stub.
pub fn create_stub_child_ref() -> fdecl::ChildRef {
    fdecl::ChildRef {
        name: STUB_COMPONENT_NAME.to_string(),
        collection: Some(STUB_COLLECTION_NAME.to_string()),
    }
}

/// Returns the id for the interface with `interface_name`.
///
/// If the interface is not found then, None is returned.
pub async fn get_interface_id<'a>(
    interface_name: &'a str,
    state_proxy: &'a fnet_interfaces::StateProxy,
) -> Result<Option<u64>> {
    let stream = fnet_interfaces_ext::event_stream_from_state(&state_proxy)
        .context("failed to get interface stream")?;
    let interfaces = fnet_interfaces_ext::existing(stream, HashMap::new())
        .await
        .context("failed to get existing interfaces")?;
    Ok(interfaces.values().find_map(
        |fidl_fuchsia_net_interfaces_ext::Properties { id, name, .. }| {
            if name == interface_name {
                Some(*id)
            } else {
                None
            }
        },
    ))
}
