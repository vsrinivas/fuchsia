// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fuchsia_component::client::connect_to_protocol, hub_report::*,
};

#[fuchsia::component]
async fn main() {
    // Create a dynamic child component
    let realm = connect_to_protocol::<fcomponent::RealmMarker>().unwrap();
    let mut collection_ref = fdecl::CollectionRef { name: String::from("coll") };
    let child_decl = fdecl::Child {
        name: Some(String::from("simple_instance")),
        url: Some(String::from("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/simple.cm")),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };

    realm
        .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
        .await
        .unwrap()
        .unwrap();

    expect_dir_listing("/hub/children", vec!["coll:simple_instance"]).await;
    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "id", "moniker", "url"],
    )
    .await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec![]).await;
    expect_file_content("/hub/children/coll:simple_instance/id", "1").await;

    // Start the dynamic child
    start_component("/hub/debug/fuchsia.sys2.LifecycleController", "./coll:simple_instance", true)
        .await;

    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "exec", "id", "moniker", "resolved", "url"],
    )
    .await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec!["child"]).await;
    expect_file_content("/hub/children/coll:simple_instance/children/child/id", "0").await;

    // Stop the dynamic child
    stop_component("/hub/debug/fuchsia.sys2.LifecycleController", "./coll:simple_instance", true)
        .await;

    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "component_type", "debug", "id", "moniker", "resolved", "url"],
    )
    .await;

    // Delete the dynamic child
    let mut child_ref = fdecl::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };

    realm.destroy_child(&mut child_ref).await.unwrap().unwrap();

    expect_dir_listing("/hub/children", vec![]).await;
}
