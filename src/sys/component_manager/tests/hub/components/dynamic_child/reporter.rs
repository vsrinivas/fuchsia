// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fuchsia_component::client::connect_to_protocol, hub_report::*,
};

#[fuchsia::main]
async fn main() {
    // Create a dynamic child component
    let mut collection_ref = fdecl::CollectionRef { name: String::from("coll") };
    let child_decl = fdecl::Child {
        name: Some("simple_instance".to_string()),
        url: Some(String::from("#meta/simple.cm")),
        startup: Some(fdecl::StartupMode::Lazy),
        environment: None,
        ..fdecl::Child::EMPTY
    };
    let realm = connect_to_protocol::<fcomponent::RealmMarker>().unwrap();

    realm
        .create_child(&mut collection_ref, child_decl, fcomponent::CreateChildArgs::EMPTY)
        .await
        .unwrap()
        .unwrap();

    expect_dir_listing("/hub/children", vec!["coll:simple_instance"]).await;
    expect_dir_listing("/hub/children/coll:simple_instance", vec!["children"]).await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec![]).await;

    // Start the dynamic child
    start_component("./coll:simple_instance", true).await;

    expect_dir_listing(
        "/hub/children/coll:simple_instance",
        vec!["children", "exposed", "ns", "out", "runtime"],
    )
    .await;
    expect_dir_listing("/hub/children/coll:simple_instance/children", vec!["child"]).await;

    // Stop the dynamic child
    stop_component("./coll:simple_instance", true).await;

    expect_dir_listing("/hub/children/coll:simple_instance", vec!["children", "exposed", "ns"])
        .await;

    // Delete the dynamic child
    let mut child_ref = fdecl::ChildRef {
        name: "simple_instance".to_string(),
        collection: Some("coll".to_string()),
    };

    realm.destroy_child(&mut child_ref).await.unwrap().unwrap();

    expect_dir_listing("/hub/children", vec![]).await;
}
