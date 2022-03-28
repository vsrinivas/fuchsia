// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fuchsia_component::client::connect_to_protocol, hub_report::*,
};

#[fuchsia::component]
async fn main() {
    test_realm_protocol().await;
    test_lifecycle_controller_protocol().await;
}

fn setup(name: &str) -> (fdecl::CollectionRef, fdecl::Child) {
    let collection_ref = fdecl::CollectionRef { name: String::from("coll") };
    let child_decl = fdecl::Child {
        name: Some(name.to_string()),
        url: Some(String::from("fuchsia-pkg://fuchsia.com/hub_integration_test#meta/simple.cm")),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };

    (collection_ref, child_decl)
}

async fn test_realm_protocol() {
    // Create a dynamic child component
    let (mut collection_ref, child_decl) = setup("simple_instance");
    let realm = connect_to_protocol::<fcomponent::RealmMarker>().unwrap();

    realm
        .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
        .await
        .unwrap()
        .unwrap();

    assert_child_exists("simple_instance", "1").await;

    // Delete the dynamic child
    let mut child_ref = fdecl::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };

    realm.destroy_child(&mut child_ref).await.unwrap().unwrap();

    expect_dir_listing("/hub/children", vec![]).await;
}

async fn test_lifecycle_controller_protocol() {
    let (mut collection_ref, child_decl) = setup("simple_instance2");

    create_component(
        "/hub/debug/fuchsia.sys2.LifecycleController",
        "./",
        &mut collection_ref,
        child_decl,
        true,
    )
    .await;

    assert_child_exists("simple_instance2", "2").await;

    // Delete the dynamic child
    let mut child_ref = fdecl::ChildRef {
        name: "simple_instance2".to_string(),
        collection: Some("coll".to_string()),
    };

    destroy_child("/hub/debug/fuchsia.sys2.LifecycleController", "./", &mut child_ref, true).await;

    expect_dir_listing("/hub/children", vec![]).await;
}

async fn assert_child_exists(name: &str, expected_id: &str) {
    expect_dir_listing("/hub/children", vec![format!("coll:{}", name).as_str()]).await;
    expect_dir_listing(
        format!("/hub/children/coll:{}", name).as_str(),
        vec!["children", "component_type", "debug", "id", "moniker", "url"],
    )
    .await;
    expect_dir_listing(format!("/hub/children/coll:{}/children", name).as_str(), vec![]).await;
    expect_file_content(format!("/hub/children/coll:{}/id", name).as_str(), expected_id).await;

    // Start the dynamic child
    start_component(
        "/hub/debug/fuchsia.sys2.LifecycleController",
        format!("./coll:{}", name).as_str(),
        true,
    )
    .await;

    expect_dir_listing(
        format!("/hub/children/coll:{}", name).as_str(),
        vec!["children", "component_type", "debug", "exec", "id", "moniker", "resolved", "url"],
    )
    .await;
    expect_dir_listing(format!("/hub/children/coll:{}/children", name).as_str(), vec!["child"])
        .await;
    expect_file_content(format!("/hub/children/coll:{}/children/child/id", name).as_str(), "0")
        .await;

    // Stop the dynamic child
    stop_component(
        "/hub/debug/fuchsia.sys2.LifecycleController",
        format!("./coll:{}", name).as_str(),
        true,
    )
    .await;

    expect_dir_listing(
        format!("/hub/children/coll:{}", name).as_str(),
        vec!["children", "component_type", "debug", "id", "moniker", "resolved", "url"],
    )
    .await;
}
