// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{format_err, Error, ResultExt},
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
async fn main() -> Result<(), Error> {
    syslog::init().context("could not initialize logging")?;
    info!("Started collection realm");
    let realm = client::connect_to_service::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    // Create a couple child components.
    println!("Creating children");
    for name in vec!["a", "b"] {
        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let child_decl = fsys::ChildDecl {
            name: Some(name.to_string()),
            url: Some(format!(
                "fuchsia-pkg://fuchsia.com/collections_integration_test#meta/trigger_{}.cm",
                name
            )),
            startup: Some(fsys::StartupMode::Lazy),
        };
        await!(realm.create_child(collection_ref, child_decl))
            .context(format!("create_child {} failed", name))?
            .expect(&format!("failed to create child {}", name));
    }

    println!("{}", await!(list_children(&realm))?);

    // Bind to children, causing them to execute.
    println!("Binding to children");
    for name in vec!["a", "b"] {
        let child_ref = new_child_ref(name, "coll");
        let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        await!(realm.bind_child(child_ref, server_end))
            .context(format!("bind_child {} failed", name))?
            .expect(&format!("failed to bind to child {}", name));
        let trigger = open_trigger_svc(&dir)?;
        await!(trigger.run()).context(format!("trigger {} failed", name))?;
    }

    // Destroy one.
    println!("Destroying child");
    {
        await!(realm.destroy_child(new_child_ref("a", "coll")))
            .context("destroy_child a failed")?
            .expect("failed to destroy child");
    }

    // Binding to destroyed child should fail.
    println!("Binding to destroyed child");
    {
        let (_, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let res = await!(realm.bind_child(new_child_ref("a", "coll"), server_end))
            .context("second bind_child a failed")?;
        let err = res.err().ok_or(format_err!("expected bind_child a to fail"))?;
        assert_eq!(err, fsys::Error::InstanceNotFound);
    }

    println!("{}", await!(list_children(&realm))?);

    // Recreate child (with different URL), and bind to it. Should work.
    println!("Recreating and binding to child");
    {
        let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
        let child_decl = fsys::ChildDecl {
            name: Some("a".to_string()),
            url: Some(
                "fuchsia-pkg://fuchsia.com/collections_integration_test#meta/trigger_realm.cm"
                    .to_string(),
            ),
            startup: Some(fsys::StartupMode::Lazy),
        };
        await!(realm.create_child(collection_ref, child_decl))
            .context("second create_child a failed")?
            .expect("failed to create second child a");
    }
    {
        let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        await!(realm.bind_child(new_child_ref("a", "coll"), server_end))
            .context("bind_child a failed")?
            .expect("failed to bind to child a");
        let trigger = open_trigger_svc(&dir)?;
        await!(trigger.run()).context("second trigger a failed")?;
    }

    println!("{}", await!(list_children(&realm))?);

    println!("Done");
    Ok(())
}

fn new_child_ref(name: &str, collection: &str) -> fsys::ChildRef {
    fsys::ChildRef { name: Some(name.to_string()), collection: Some(collection.to_string()) }
}

async fn list_children(realm: &fsys::RealmProxy) -> Result<String, Error> {
    let (iterator_proxy, server_end) = endpoints::create_proxy().unwrap();
    let collection_ref = fsys::CollectionRef { name: Some("coll".to_string()) };
    await!(realm.list_children(collection_ref, server_end))
        .context("list_children failed")?
        .expect("failed to list children");
    let res = await!(iterator_proxy.next());
    let children = res.expect("failed to iterate over children");
    let children: Vec<_> = children
        .iter()
        .map(|c| {
            format!(
                "{}:{}",
                c.collection.as_ref().expect("no name"),
                c.name.as_ref().expect("no collection")
            )
        })
        .collect();
    Ok(format!("Found children ({})", children.join(",")))
}

fn open_trigger_svc(dir: &DirectoryProxy) -> Result<ftest::TriggerProxy, Error> {
    let node_proxy = io_util::open_node(
        dir,
        &PathBuf::from("svc/fidl.test.components.Trigger"),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .context("failed to open trigger service")?;
    let trigger = ftest::TriggerProxy::new(node_proxy.into_channel().unwrap());
    Ok(trigger)
}
