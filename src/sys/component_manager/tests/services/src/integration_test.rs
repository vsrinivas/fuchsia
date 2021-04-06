// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! These integration tests exercise Services exposed from collections.

use {
    cm_rust::{ExposeDecl, ExposeServiceDecl, ExposeSource, ExposeTarget, ServiceSource},
    fidl::endpoints::UnifiedServiceMarker,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys2, fidl_fuchsia_test_services as ftest,
    fuchsia_component::client::ScopedInstance,
    fuchsia_component_test::builder::{
        Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint,
    },
};

const COLLECTION_NAME: &'static str = "coll";

#[fuchsia::test]
async fn empty_collection_lists_empty_service() {
    let instance = start_component_under_test().await;
    let dir = instance.get_exposed_dir();
    let service_dir = io_util::directory::open_directory(
        dir,
        ftest::ServiceMarker::SERVICE_NAME,
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .await
    .expect("failed to open Services");
    let entries = files_async::readdir(&service_dir).await.expect("failed to list instances");
    assert_eq!(entries.len(), 0);
}

#[fuchsia::test]
async fn collection_with_single_component_lists_single_service_instance() {
    let instance = start_component_under_test().await;
    let dir = instance.get_exposed_dir();
    let realm =
        fuchsia_component::client::connect_to_protocol_at_dir_root::<fsys2::RealmMarker>(dir)
            .expect("failed to connect to Realm");
    start_dynamic(
        &realm,
        "a",
        "fuchsia-pkg://fuchsia.com/services-integration-tests#meta/instance-a.cm",
    )
    .await;
    let service_dir = io_util::directory::open_directory(
        dir,
        ftest::ServiceMarker::SERVICE_NAME,
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .await
    .expect("failed to open Services");
    let entries = files_async::readdir(&service_dir).await.expect("failed to list instances");
    assert_eq!(
        entries,
        vec![files_async::DirEntry {
            kind: files_async::DirentKind::Directory,
            name: "a".to_string()
        }]
    );
}

#[fuchsia::test]
async fn collection_with_many_components_lists_many_service_instances() {
    let instance = start_component_under_test().await;
    let dir = instance.get_exposed_dir();
    let realm =
        fuchsia_component::client::connect_to_protocol_at_dir_root::<fsys2::RealmMarker>(dir)
            .expect("failed to connect to Realm");
    start_dynamic(
        &realm,
        "a",
        "fuchsia-pkg://fuchsia.com/services-integration-tests#meta/instance-a.cm",
    )
    .await;
    start_dynamic(
        &realm,
        "b",
        "fuchsia-pkg://fuchsia.com/services-integration-tests#meta/instance-b.cm",
    )
    .await;
    let service_dir = io_util::directory::open_directory(
        dir,
        ftest::ServiceMarker::SERVICE_NAME,
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )
    .await
    .expect("failed to open Services");
    let entries = files_async::readdir(&service_dir).await.expect("failed to list instances");
    assert_eq!(
        entries,
        vec![
            files_async::DirEntry {
                kind: files_async::DirentKind::Directory,
                name: "a".to_string()
            },
            files_async::DirEntry {
                kind: files_async::DirentKind::Directory,
                name: "b".to_string()
            }
        ]
    );
}

#[fuchsia::test]
async fn collection_with_many_components_opens_many_service_instances() {
    let instance = start_component_under_test().await;
    let dir = instance.get_exposed_dir();
    let realm =
        fuchsia_component::client::connect_to_protocol_at_dir_root::<fsys2::RealmMarker>(dir)
            .expect("failed to connect to Realm");
    start_dynamic(
        &realm,
        "a",
        "fuchsia-pkg://fuchsia.com/services-integration-tests#meta/instance-a.cm",
    )
    .await;
    start_dynamic(
        &realm,
        "b",
        "fuchsia-pkg://fuchsia.com/services-integration-tests#meta/instance-b.cm",
    )
    .await;

    let proxy = fuchsia_component::client::connect_to_unified_service_instance_in_dir_at::<
        ftest::ServiceMarker,
    >(&dir, ftest::ServiceMarker::SERVICE_NAME, "a")
    .expect("failed to connect to unified service instance a");
    let instance_reporter =
        proxy.instance_reporter().expect("failed to get InstanceReporter protocol");
    assert_eq!(instance_reporter.report_instance().await.expect("failed to report_instance"), "a");

    let proxy = fuchsia_component::client::connect_to_unified_service_instance_in_dir_at::<
        ftest::ServiceMarker,
    >(&dir, ftest::ServiceMarker::SERVICE_NAME, "b")
    .expect("failed to connect to unified service instance b");
    let instance_reporter =
        proxy.instance_reporter().expect("failed to get InstanceReporter protocol");
    assert_eq!(instance_reporter.report_instance().await.expect("failed to report_instance"), "b");
}

async fn start_component_under_test() -> ScopedInstance {
    let mut builder = RealmBuilder::new().await.expect("failed to create RealmBuilder");
    builder
        .add_component(
            "root",
            ComponentSource::url(
                "fuchsia-pkg://fuchsia.com/services-integration-tests#meta/collection.cm",
            ),
        )
        .await
        .expect("failed to add root component");
    builder
        .add_route(CapabilityRoute {
            capability: Capability::Protocol("fuchsia.sys2.Realm".to_string()),
            source: RouteEndpoint::component("root"),
            targets: vec![RouteEndpoint::AboveRoot],
        })
        .expect("failed to add route for fuchsia.sys2.Realm");
    builder
        .add_route(CapabilityRoute {
            capability: Capability::Protocol("fuchsia.logger.LogSink".to_string()),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("root")],
        })
        .expect("failed to add route for fuchsia.logger.LogSink");
    let mut topology = builder.build();
    let decl = topology.get_decl_mut(&"".into()).expect("could not find ComponentDecl for root");
    decl.exposes.push(ExposeDecl::Service(ExposeServiceDecl {
        sources: vec![ServiceSource {
            source_name: ftest::ServiceMarker::SERVICE_NAME.into(),
            source: ExposeSource::Child("root".to_string()),
        }],
        target: ExposeTarget::Parent,
        target_name: ftest::ServiceMarker::SERVICE_NAME.into(),
    }));
    topology.create().await.expect("failed to create topology").root
}

async fn start_dynamic(realm: &fsys2::RealmProxy, name: &str, url: &str) -> fio::DirectoryProxy {
    realm
        .create_child(
            &mut fsys2::CollectionRef { name: COLLECTION_NAME.to_string() },
            fsys2::ChildDecl {
                name: Some(name.to_string()),
                url: Some(url.to_string()),
                startup: Some(fsys2::StartupMode::Lazy),
                environment: None,
                ..fsys2::ChildDecl::EMPTY
            },
        )
        .await
        .expect("failed to make create_child FIDL call")
        .expect("failed to create_child");

    let (exposed_dir, exposed_dir_server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("failed to create endpoints");
    realm
        .bind_child(
            &mut fsys2::ChildRef {
                name: name.to_string(),
                collection: Some(COLLECTION_NAME.to_string()),
            },
            exposed_dir_server_end,
        )
        .await
        .expect("failed to make bind_child FIDL call")
        .expect("failed to bind_child");
    exposed_dir
}
