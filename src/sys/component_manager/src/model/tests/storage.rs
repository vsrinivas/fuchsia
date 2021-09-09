// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::BindReason,
        error::ModelError,
        rights,
        routing::{route_and_open_capability, OpenOptions, OpenStorageOptions},
        testing::routing_test_helpers::*,
    },
    ::routing_test_helpers::{
        component_id_index::make_index_file, storage::CommonStorageTest, RoutingTestModel,
    },
    cm_rust::*,
    cm_rust_testing::*,
    component_id_index::gen_instance_id,
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMoniker, RelativeMonikerBase},
    routing::{error::RoutingError, RouteRequest},
    std::{convert::TryInto, path::PathBuf},
};

#[fuchsia::test]
async fn storage_dir_from_cm_namespace() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_storage_dir_from_cm_namespace().await
}

#[fuchsia::test]
async fn storage_and_dir_from_parent() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_storage_and_dir_from_parent().await
}

#[fuchsia::test]
async fn storage_and_dir_from_parent_with_subdir() {
    CommonStorageTest::<RoutingTestBuilder>::new()
        .test_storage_and_dir_from_parent_with_subdir()
        .await
}

#[fuchsia::test]
async fn storage_and_dir_from_parent_rights_invalid() {
    CommonStorageTest::<RoutingTestBuilder>::new()
        .test_storage_and_dir_from_parent_rights_invalid()
        .await
}

#[fuchsia::test]
async fn storage_from_parent_dir_from_grandparent() {
    CommonStorageTest::<RoutingTestBuilder>::new()
        .test_storage_from_parent_dir_from_grandparent()
        .await
}

#[fuchsia::test]
async fn storage_from_parent_dir_from_grandparent_with_subdirs() {
    CommonStorageTest::<RoutingTestBuilder>::new()
        .test_storage_from_parent_dir_from_grandparent_with_subdirs()
        .await
}

#[fuchsia::test]
async fn storage_from_parent_dir_from_grandparent_with_subdir() {
    CommonStorageTest::<RoutingTestBuilder>::new()
        .test_storage_from_parent_dir_from_grandparent_with_subdir()
        .await
}

#[fuchsia::test]
async fn storage_and_dir_from_grandparent() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_storage_and_dir_from_grandparent().await
}

#[fuchsia::test]
async fn storage_from_parent_dir_from_sibling() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_storage_from_parent_dir_from_sibling().await
}

#[fuchsia::test]
async fn storage_from_parent_dir_from_sibling_with_subdir() {
    CommonStorageTest::<RoutingTestBuilder>::new()
        .test_storage_from_parent_dir_from_sibling_with_subdir()
        .await
}

///   a
///    \
///     b
///      \
///      [c]
///
/// a: offers directory to b at path /minfs
/// b: has storage decl with name "mystorage" with a source of realm at path /data
/// b: offers storage to collection from "mystorage"
/// [c]: uses storage as /storage
/// [c]: destroyed and storage goes away
#[fuchsia::test]
async fn use_in_collection_from_parent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferSource::Self_,
                    source_name: "data".try_into().unwrap(),
                    target_name: "minfs".try_into().unwrap(),
                    target: OfferTarget::static_child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.sys2.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some(PathBuf::from("data")),
                    storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
                })
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some(PathBuf::from("cache")),
                    storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
                })
                .add_transient_collection("coll")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/cache".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_dynamic_child(
        vec!["b:0"].into(),
        "coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // Use storage and confirm its existence.
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["coll:c:1".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("data".to_string()),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["coll:c:1".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("cache".to_string()),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    // Confirm storage directory exists for component in collection
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            RelativeMoniker::new(vec![], vec![]),
            None,
            ""
        )
        .await,
        vec!["coll:c:1".to_string()],
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            RelativeMoniker::new(vec![], vec![]),
            None,
            ""
        )
        .await,
        vec!["coll:c:1".to_string()],
    );
    test.destroy_dynamic_child(vec!["b:0"].into(), "coll", "c").await;

    // Confirm storage no longer exists.
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            RelativeMoniker::new(vec![], vec![]),
            None,
            ""
        )
        .await,
        Vec::<String>::new(),
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            RelativeMoniker::new(vec![], vec![]),
            None,
            ""
        )
        .await,
        Vec::<String>::new(),
    );
}

///   a
///    \
///     b
///      \
///      [c]
///
/// a: has storage decl with name "mystorage" with a source of self at path /data
/// a: offers storage to b from "mystorage"
/// b: offers storage to collection from "mystorage"
/// [c]: uses storage as /storage
/// [c]: destroyed and storage goes away
#[fuchsia::test]
async fn use_in_collection_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("minfs")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: Some(PathBuf::from("data")),
                    storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
                })
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: Some(PathBuf::from("cache")),
                    storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.sys2.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_transient_collection("coll")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/cache".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_dynamic_child(
        vec!["b:0"].into(),
        "coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // Use storage and confirm its existence.
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(
                vec![],
                vec!["b:0".into(), "coll:c:1".into()],
            )),
            from_cm_namespace: false,
            storage_subdir: Some("data".to_string()),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(
                vec![],
                vec!["b:0".into(), "coll:c:1".into()],
            )),
            from_cm_namespace: false,
            storage_subdir: Some("cache".to_string()),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
            None,
            "children",
        )
        .await,
        vec!["coll:c:1".to_string()]
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
            None,
            "children",
        )
        .await,
        vec!["coll:c:1".to_string()]
    );
    test.destroy_dynamic_child(vec!["b:0"].into(), "coll", "c").await;

    // Confirm storage no longer exists.
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
            None,
            "children"
        )
        .await,
        Vec::<String>::new(),
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
            None,
            "children"
        )
        .await,
        Vec::<String>::new(),
    );
}

#[fuchsia::test]
async fn storage_multiple_types() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_storage_multiple_types().await
}

#[fuchsia::test]
async fn use_the_wrong_type_of_storage() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_use_the_wrong_type_of_storage().await
}

#[fuchsia::test]
async fn directories_are_not_storage() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_directories_are_not_storage().await
}

#[fuchsia::test]
async fn use_storage_when_not_offered() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_use_storage_when_not_offered().await
}

#[fuchsia::test]
async fn dir_offered_from_nonexecutable() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_dir_offered_from_nonexecutable().await
}

#[fuchsia::test]
async fn storage_dir_from_cm_namespace_prevented_by_policy() {
    CommonStorageTest::<RoutingTestBuilder>::new()
        .test_storage_dir_from_cm_namespace_prevented_by_policy()
        .await
}

#[fuchsia::test]
async fn instance_id_from_index() {
    CommonStorageTest::<RoutingTestBuilder>::new().test_instance_id_from_index().await
}

///   component manager's namespace
///    |
///   provider (provides storage capability, restricted to component ID index)
///    |
///   parent_consumer (in component ID index)
///    |
///   child_consumer (not in component ID index)
///
/// Test that a component cannot start if it uses restricted storage but isn't in the component ID
/// index.
#[fuchsia::test]
async fn use_restricted_storage_start_failure() {
    let parent_consumer_instance_id = Some(gen_instance_id(&mut rand::thread_rng()));
    let component_id_index_path = make_index_file(component_id_index::Index {
        instances: vec![component_id_index::InstanceIdEntry {
            instance_id: parent_consumer_instance_id.clone(),
            appmgr_moniker: None,
            moniker: Some(
                AbsoluteMoniker::parse_string_without_instances("/parent_consumer").unwrap(),
            ),
        }],
        ..component_id_index::Index::default()
    })
    .unwrap();
    let components = vec![
        (
            "provider",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fsys::StorageId::StaticInstanceId,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("parent_consumer".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("parent_consumer")
                .build(),
        ),
        (
            "parent_consumer",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::static_child("child_consumer".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("child_consumer")
                .build(),
        ),
        (
            "child_consumer",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTestBuilder::new("provider", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    test.bind_instance(
        &AbsoluteMoniker::parse_string_without_instances("/parent_consumer").unwrap(),
    )
    .await
    .expect("bind to /parent_consumer failed");

    let child_consumer_moniker =
        AbsoluteMoniker::parse_string_without_instances("/parent_consumer/child_consumer").unwrap();
    let child_bind_result = test.bind_instance(&child_consumer_moniker).await;
    assert!(matches!(
        child_bind_result,
        Err(ModelError::RoutingError { err: RoutingError::ComponentNotInIdIndex { moniker: _ } })
    ));
}

///   component manager's namespace
///    |
///   provider (provides storage capability, restricted to component ID index)
///    |
///   parent_consumer (in component ID index)
///    |
///   child_consumer (not in component ID index)
///
/// Test that a component cannot open a restricted storage capability if the component isn't in
/// the component iindex.
#[fuchsia::test]
async fn use_restricted_storage_open_failure() {
    let parent_consumer_instance_id = Some(gen_instance_id(&mut rand::thread_rng()));
    let component_id_index_path = make_index_file(component_id_index::Index {
        instances: vec![component_id_index::InstanceIdEntry {
            instance_id: parent_consumer_instance_id.clone(),
            appmgr_moniker: None,
            moniker: Some(
                AbsoluteMoniker::parse_string_without_instances("/parent_consumer/child_consumer")
                    .unwrap(),
            ),
        }],
        ..component_id_index::Index::default()
    })
    .unwrap();
    let components = vec![
        (
            "provider",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("parent_consumer".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("parent_consumer")
                .build(),
        ),
        (
            "parent_consumer",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTestBuilder::new("provider", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    let parent_consumer_moniker =
        AbsoluteMoniker::parse_string_without_instances("/parent_consumer").unwrap();
    let parent_consumer_instance = test
        .bind_and_get_instance(&parent_consumer_moniker, BindReason::Eager, false)
        .await
        .expect("could not resolve state");

    // `parent_consumer` should be able to open its storage because its not restricted
    let (_client_end, mut server_end) =
        zx::Channel::create().expect("could not create storage dir endpoints");
    route_and_open_capability(
        RouteRequest::UseStorage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/storage".try_into().unwrap(),
        }),
        &parent_consumer_instance,
        OpenOptions::Storage(OpenStorageOptions {
            open_mode: fio::MODE_TYPE_DIRECTORY,
            bind_reason: BindReason::Eager,
            server_chan: &mut server_end,
        }),
    )
    .await
    .expect("Unable to route.  oh no!!");

    // now modify StorageDecl so that it restricts storage
    let provider_instance = test
        .bind_and_get_instance(
            &AbsoluteMoniker::parse_string_without_instances("/").unwrap(),
            BindReason::Eager,
            false,
        )
        .await
        .expect("could not resolve state");
    {
        let mut resolved_state = provider_instance.lock_resolved_state().await.unwrap();
        for cap in resolved_state.decl_as_mut().capabilities.iter_mut() {
            match cap {
                CapabilityDecl::Storage(storage_decl) => {
                    storage_decl.storage_id = fsys::StorageId::StaticInstanceId;
                }
                _ => {}
            }
        }
    }

    // `parent_consumer` should NOT be able to open its storage because its IS restricted
    let (_client_end, mut server_end) =
        zx::Channel::create().expect("could not create storage dir endpoints");
    let result = route_and_open_capability(
        RouteRequest::UseStorage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/storage".try_into().unwrap(),
        }),
        &parent_consumer_instance,
        OpenOptions::Storage(OpenStorageOptions {
            open_mode: fio::MODE_TYPE_DIRECTORY,
            bind_reason: BindReason::Eager,
            server_chan: &mut server_end,
        }),
    )
    .await;
    assert!(matches!(
        result,
        Err(ModelError::RoutingError { err: RoutingError::ComponentNotInIdIndex { moniker: _ } })
    ));
}
