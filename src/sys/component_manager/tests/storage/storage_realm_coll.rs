// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::create_proxy,
    fidl_fidl_test_components as ftest, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Create the dynamic child
    let realm = connect_to_service::<fsys::RealmMarker>().context("error connecting to realm")?;
    let mut collection_ref = fsys::CollectionRef { name: String::from("coll") };
    let child_decl = fsys::ChildDecl {
        name: Some(String::from("storage_user")),
        url: Some(String::from(
            "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_user.cm",
        )),
        startup: Some(fsys::StartupMode::Lazy),
    };

    realm
        .create_child(&mut collection_ref, child_decl)
        .await
        .context("create_child failed")?
        .expect("failed to create child");

    // Bind to child
    let mut child_ref =
        fsys::ChildRef { name: "storage_user".to_string(), collection: Some("coll".to_string()) };
    let (_, server_end) = create_proxy::<fidl_fuchsia_io::DirectoryMarker>()?;

    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .context("bind_child failed")?
        .expect("failed to bind child");

    let trigger =
        connect_to_service::<ftest::TriggerMarker>().context("error connecting to trigger")?;
    trigger.run().await?;

    // Destroy child
    realm
        .destroy_child(&mut child_ref)
        .await
        .context("delete_child failed")?
        .expect("failed to delete child");

    Ok(())
}
