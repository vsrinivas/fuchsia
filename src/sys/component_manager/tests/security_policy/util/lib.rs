// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    component_events::{events::*, matcher::EventMatcher, sequence::{EventSequence, Ordering}},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_component,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
};

pub async fn start_policy_test(
    component_manager_url: &str,
    root_component_url: &str,
) -> Result<(RealmInstance, fcomponent::RealmProxy, EventStream), Error> {
    let builder = RealmBuilder::new().await.unwrap();
    let root_child =
        builder.add_child("root", root_component_url, ChildOptions::new().eager()).await.unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&root_child),
        )
        .await
        .unwrap();
    let instance = builder.build_in_nested_component_manager(component_manager_url).await.unwrap();
    let proxy =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::EventStream2Marker>().unwrap();

    let event_stream = EventStream::new_v2(proxy);

    instance.start_component_tree().await.unwrap();

    // Wait for the root component to be started so we can connect to its Realm service through the
    // hub.
    let event_stream = EventSequence::new().has_subset(vec![EventMatcher::ok().r#type(Started::TYPE).moniker("./root")], Ordering::Unordered).expect_and_giveback(event_stream).await.unwrap();
    // Get to the Realm protocol
    let realm_query =
        instance.root.connect_to_protocol_at_exposed_dir::<fsys::RealmQueryMarker>().unwrap();
    let (_, resolved) = realm_query.get_instance_info("./root").await.unwrap().unwrap();
    let exposed_dir = resolved.unwrap().exposed_dir.into_proxy().unwrap();

    let realm =
        fuchsia_component::client::connect_to_protocol_at_dir_root::<fcomponent::RealmMarker>(
            &exposed_dir,
        )
        .unwrap();

    Ok((instance, realm, event_stream))
}

pub async fn open_exposed_dir(
    realm: &fcomponent::RealmProxy,
    name: &str,
) -> Result<fio::DirectoryProxy, fcomponent::Error> {
    let mut child_ref = fdecl::ChildRef { name: name.to_string(), collection: None };
    let (exposed_dir, server_end) = create_proxy().unwrap();
    realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .expect("open_exposed_dir failed")
        .map(|_| exposed_dir)
}
