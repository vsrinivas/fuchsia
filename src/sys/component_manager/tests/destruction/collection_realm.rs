// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints,
    fidl_fidl_test_components as ftest,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, MODE_TYPE_SERVICE},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_syslog as syslog,
    io_util::{self, OPEN_RIGHT_READABLE},
    log::*,
    std::path::PathBuf,
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&[]).expect("could not initialize logging");
    info!("Started collection realm");
    let realm = client::connect_to_service::<fsys::RealmMarker>()
        .expect("could not connect to Realm service");

    // Create a "trigger realm" child component.
    info!("Creating child");
    {
        let mut collection_ref = fsys::CollectionRef { name: "coll".to_string() };
        let child_decl = fsys::ChildDecl {
            name: Some("root".to_string()),
            url: Some(
                "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/trigger_realm.cm"
                    .to_string(),
            ),
            startup: Some(fsys::StartupMode::Lazy),
        };
        realm
            .create_child(&mut collection_ref, child_decl)
            .await
            .expect(&format!("create_child failed"))
            .expect(&format!("failed to create child"));
    }

    // Bind to child, causing it to start (along with its eager children).
    info!("Binding to child");
    {
        let mut child_ref =
            fsys::ChildRef { name: "root".to_string(), collection: Some("coll".to_string()) };
        let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        realm
            .bind_child(&mut child_ref, server_end)
            .await
            .expect(&format!("bind_child failed"))
            .expect(&format!("failed to bind to child"));
        let trigger = open_trigger_svc(&dir).expect("failed to open trigger service");
        trigger.run().await.expect("trigger failed");
    }

    // Destroy the child.
    info!("Destroying child");
    {
        let mut child_ref =
            fsys::ChildRef { name: "root".to_string(), collection: Some("coll".to_string()) };
        realm
            .destroy_child(&mut child_ref)
            .await
            .expect("destroy_child failed")
            .expect("failed to destroy child");
    }

    info!("Done");
}

fn open_trigger_svc(dir: &DirectoryProxy) -> Result<ftest::TriggerProxy, Error> {
    let node_proxy = io_util::open_node(
        dir,
        &PathBuf::from("svc/fidl.test.components.Trigger"),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .context("failed to open trigger service")?;
    Ok(ftest::TriggerProxy::new(node_proxy.into_channel().unwrap()))
}
