// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    component_events::{events::*, matcher::EventMatcher},
    fidl::endpoints::{create_proxy, DiscoverableProtocolMarker},
    fidl_fuchsia_component as fcomp,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fuchsia_component,
    test_utils_lib::opaque_test::{OpaqueTest, OpaqueTestBuilder},
};

fn connect_to_root_service<P: DiscoverableProtocolMarker>(
    test: &OpaqueTest,
) -> Result<P::Proxy, Error> {
    let mut service_path = test.get_hub_v2_path();
    service_path.extend(&["exec", "expose", P::PROTOCOL_NAME]);
    fuchsia_component::client::connect_to_protocol_at_path::<P>(service_path.to_str().unwrap())
}

pub async fn start_policy_test(
    component_manager_url: &str,
    root_component_url: &str,
    config_path: &str,
) -> Result<(OpaqueTest, fsys::RealmProxy, EventStream), Error> {
    let test = OpaqueTestBuilder::new(root_component_url)
        .component_manager_url(component_manager_url)
        .config(config_path)
        .build()
        .await?;
    let event_source = test.connect_to_event_source().await?;
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Started::NAME, Stopped::NAME],
            EventMode::Async,
        )])
        .await?;
    event_source.start_component_tree().await;

    // Wait for the root component to be started so we can connect to its Realm service through the
    // hub.
    EventMatcher::ok().moniker(".").expect_match::<Started>(&mut event_stream).await;

    let realm = connect_to_root_service::<fsys::RealmMarker>(&test)
        .context("failed to connect to root sys2.Realm")?;
    Ok((test, realm, event_stream))
}

pub async fn open_exposed_dir(
    realm: &fsys::RealmProxy,
    name: &str,
) -> Result<DirectoryProxy, fcomp::Error> {
    let mut child_ref = fsys::ChildRef { name: name.to_string(), collection: None };
    let (exposed_dir, server_end) = create_proxy().unwrap();
    realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .expect("open_exposed_dir failed")
        .map(|_| exposed_dir)
}
