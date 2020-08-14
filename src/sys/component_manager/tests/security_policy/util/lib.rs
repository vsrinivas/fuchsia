// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::endpoints::{create_proxy, DiscoverableService},
    fidl_fuchsia_component as fcomp,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fuchsia_component,
    test_utils_lib::{
        events::*,
        test_utils::{OpaqueTest, OpaqueTestBuilder},
    },
};

fn connect_to_root_service<S: DiscoverableService>(test: &OpaqueTest) -> Result<S::Proxy, Error> {
    let mut service_path = test.get_hub_v2_path();
    service_path.extend(&["exec", "expose", "svc", S::SERVICE_NAME]);
    fuchsia_component::client::connect_to_service_at_path::<S>(service_path.to_str().unwrap())
}

pub async fn start_policy_test(
    component_manager_url: &str,
    root_component_url: &str,
    config_path: &str,
) -> Result<(OpaqueTest, fsys::RealmProxy), Error> {
    let test = OpaqueTestBuilder::new(root_component_url)
        .component_manager_url(component_manager_url)
        .config(config_path)
        .build()
        .await?;
    let event_source = test.connect_to_event_source().await?;
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await?;
    event_source.start_component_tree().await;

    // Wait for the root component to be started so we can connect to its Realm service through the
    // hub.
    let event = event_stream.expect_exact::<Started>(EventMatcher::new().expect_moniker(".")).await;
    event.resume().await?;

    let realm = connect_to_root_service::<fsys::RealmMarker>(&test)
        .context("failed to connect to root sys2.Realm")?;
    Ok((test, realm))
}

pub async fn bind_child(
    realm: &fsys::RealmProxy,
    name: &str,
) -> Result<DirectoryProxy, fcomp::Error> {
    let mut child_ref = fsys::ChildRef { name: name.to_string(), collection: None };
    let (exposed_dir, server_end) = create_proxy().unwrap();
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .expect("binding child failed")
        .map(|_| exposed_dir)
}
