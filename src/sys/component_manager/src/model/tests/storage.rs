// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::StartReason,
        error::ModelError,
        routing::{route_and_open_capability, OpenOptions, OpenStorageOptions},
        testing::routing_test_helpers::*,
    },
    ::routing_test_helpers::{
        component_id_index::make_index_file, storage::CommonStorageTest, RoutingTestModel,
    },
    cm_moniker::InstancedRelativeMoniker,
    cm_rust::*,
    cm_rust_testing::*,
    component_id_index::gen_instance_id,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon as zx,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, RelativeMonikerBase},
    routing::{error::RoutingError, RouteRequest},
    std::{
        convert::TryInto,
        path::{Path, PathBuf},
    },
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
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferSource::Self_,
                    source_name: "data".try_into().unwrap(),
                    target_name: "minfs".try_into().unwrap(),
                    target: OfferTarget::static_child("b".to_string()),
                    rights: Some(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    availability: Availability::Required,
                }))
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some(PathBuf::from("data")),
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some(PathBuf::from("cache")),
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
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
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/cache".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_dynamic_child(
        vec!["b"].into(),
        "coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // Use storage and confirm its existence.
    test.check_use(
        vec!["b", "coll:c"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec!["coll:c:1".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("data".to_string()),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use(
        vec!["b", "coll:c"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec!["coll:c:1".into()])),
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
            InstancedRelativeMoniker::new(vec![]),
            None,
            ""
        )
        .await,
        vec!["coll:c:1".to_string()],
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            InstancedRelativeMoniker::new(vec![]),
            None,
            ""
        )
        .await,
        vec!["coll:c:1".to_string()],
    );
    test.destroy_dynamic_child(vec!["b"].into(), "coll", "c").await;

    // Confirm storage no longer exists.
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            InstancedRelativeMoniker::new(vec![]),
            None,
            ""
        )
        .await,
        Vec::<String>::new(),
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            InstancedRelativeMoniker::new(vec![]),
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
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    availability: Availability::Required,
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: Some(PathBuf::from("data")),
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: Some(PathBuf::from("cache")),
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    availability: Availability::Required,
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
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/cache".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_dynamic_child(
        vec!["b"].into(),
        "coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // Use storage and confirm its existence.
    test.check_use(
        vec!["b", "coll:c"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "coll:c:1".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: Some("data".to_string()),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use(
        vec!["b", "coll:c"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "coll:c:1".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: Some("cache".to_string()),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            InstancedRelativeMoniker::new(vec!["b:0".into()]),
            None,
            "children",
        )
        .await,
        vec!["coll:c:1".to_string()]
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            InstancedRelativeMoniker::new(vec!["b:0".into()]),
            None,
            "children",
        )
        .await,
        vec!["coll:c:1".to_string()]
    );
    test.destroy_dynamic_child(vec!["b"].into(), "coll", "c").await;

    // Confirm storage no longer exists.
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            InstancedRelativeMoniker::new(vec!["b:0".into()]),
            None,
            "children"
        )
        .await,
        Vec::<String>::new(),
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            InstancedRelativeMoniker::new(vec!["b:0".into()]),
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
            moniker: Some(AbsoluteMoniker::parse_str("/parent_consumer").unwrap()),
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
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceId,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("parent_consumer".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    availability: Availability::Required,
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
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::static_child("child_consumer".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    availability: Availability::Required,
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
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];
    let test = RoutingTestBuilder::new("provider", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    test.start_instance(&AbsoluteMoniker::parse_str("/parent_consumer").unwrap())
        .await
        .expect("start /parent_consumer failed");

    let child_bind_result = test
        .start_instance(&AbsoluteMoniker::parse_str("/parent_consumer/child_consumer").unwrap())
        .await;
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
            moniker: Some(AbsoluteMoniker::parse_str("/parent_consumer/child_consumer").unwrap()),
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
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("parent_consumer".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    availability: Availability::Required,
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
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];
    let test = RoutingTestBuilder::new("provider", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    let parent_consumer_moniker = AbsoluteMoniker::parse_str("/parent_consumer").unwrap();
    let (parent_consumer_instance, _) = test
        .start_and_get_instance(&parent_consumer_moniker, StartReason::Eager, false)
        .await
        .expect("could not resolve state");

    // `parent_consumer` should be able to open its storage because its not restricted
    let (_client_end, mut server_end) =
        zx::Channel::create().expect("could not create storage dir endpoints");
    route_and_open_capability(
        RouteRequest::UseStorage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/storage".try_into().unwrap(),
            availability: cm_rust::Availability::Required,
        }),
        &parent_consumer_instance,
        OpenOptions::Storage(OpenStorageOptions {
            flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            open_mode: fio::MODE_TYPE_DIRECTORY,
            relative_path: ".".into(),
            server_chan: &mut server_end,
        }),
    )
    .await
    .expect("Unable to route.  oh no!!");

    // now modify StorageDecl so that it restricts storage
    let (provider_instance, _) = test
        .start_and_get_instance(&AbsoluteMoniker::root(), StartReason::Eager, false)
        .await
        .expect("could not resolve state");
    {
        let mut resolved_state = provider_instance.lock_resolved_state().await.unwrap();
        for cap in resolved_state.decl_as_mut().capabilities.iter_mut() {
            match cap {
                CapabilityDecl::Storage(storage_decl) => {
                    storage_decl.storage_id = fdecl::StorageId::StaticInstanceId;
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
            availability: cm_rust::Availability::Required,
        }),
        &parent_consumer_instance,
        OpenOptions::Storage(OpenStorageOptions {
            flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            open_mode: fio::MODE_TYPE_DIRECTORY,
            relative_path: ".".into(),
            server_chan: &mut server_end,
        }),
    )
    .await;
    assert!(matches!(
        result,
        Err(ModelError::RoutingError { err: RoutingError::ComponentNotInIdIndex { moniker: _ } })
    ));
}

///   component manager's namespace
///    |
///   provider (provides storage capability, restricted to component ID index)
///    |
///   parent_consumer (in component ID index)
///
/// Test that a component can open a subdirectory of a storage successfully
#[fuchsia::test]
async fn open_storage_subdirectory() {
    let parent_consumer_instance_id = Some(gen_instance_id(&mut rand::thread_rng()));
    let component_id_index_path = make_index_file(component_id_index::Index {
        instances: vec![component_id_index::InstanceIdEntry {
            instance_id: parent_consumer_instance_id.clone(),
            appmgr_moniker: None,
            moniker: Some(AbsoluteMoniker::parse_str("/consumer").unwrap()),
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
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "cache".into(),
                    backing_dir: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("consumer".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    availability: Availability::Required,
                }))
                .add_lazy_child("consumer")
                .build(),
        ),
        (
            "consumer",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];
    let test = RoutingTestBuilder::new("provider", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    let consumer_moniker = AbsoluteMoniker::parse_str("/consumer").unwrap();
    let (consumer_instance, _) = test
        .start_and_get_instance(&consumer_moniker, StartReason::Eager, false)
        .await
        .expect("could not resolve state");

    // `consumer` should be able to open its storage at the root dir
    let (root_dir, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let mut server_end = server_end.into_channel();
    route_and_open_capability(
        RouteRequest::UseStorage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/storage".try_into().unwrap(),
            availability: cm_rust::Availability::Required,
        }),
        &consumer_instance,
        OpenOptions::Storage(OpenStorageOptions {
            flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            open_mode: fio::MODE_TYPE_DIRECTORY,
            relative_path: ".".into(),
            server_chan: &mut server_end,
        }),
    )
    .await
    .expect("Unable to route.  oh no!!");

    // Create the subdirectories we will open later
    let bar_dir = fuchsia_fs::directory::create_directory_recursive(
        &root_dir,
        "foo/bar",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )
    .await
    .unwrap();
    let entries = fuchsia_fs::directory::readdir(&bar_dir).await.unwrap();
    assert!(entries.is_empty());

    // `consumer` should be able to open its storage at "foo/bar"
    let (bar_dir, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    let mut server_end = server_end.into_channel();
    route_and_open_capability(
        RouteRequest::UseStorage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/storage".try_into().unwrap(),
            availability: cm_rust::Availability::Required,
        }),
        &consumer_instance,
        OpenOptions::Storage(OpenStorageOptions {
            flags: fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            open_mode: fio::MODE_TYPE_DIRECTORY,
            relative_path: "foo/bar".into(),
            server_chan: &mut server_end,
        }),
    )
    .await
    .expect("Unable to route.  oh no!!");

    let entries = fuchsia_fs::directory::readdir(&bar_dir).await.unwrap();
    assert!(entries.is_empty());
}

///   a
///   |
///   b
///   |
///  coll-persistent_storage: "true"
///   |
/// [c:1]
///
/// Test that storage data persists after destroy for a collection with a moniker-based storage
/// path. The persistent storage data can be deleted through a
/// StorageAdminRequest::DeleteComponentStorage request.
/// The following storage paths are used:
///  - moniker path with instance ids cleared
#[fuchsia::test]
async fn storage_persistence_relative_moniker_path() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("minfs")
                        .path("/data")
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Capability("data".into()),
                    source_name: "fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    target_name: "fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    target: OfferTarget::static_child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_name: "fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("persistent_coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_collection(
                    CollectionDeclBuilder::new_transient_collection("persistent_coll")
                        .persistent_storage(true),
                )
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];

    let test = RoutingTestBuilder::new("a", components).build().await;

    // create [c:1] under the storage persistent collection
    test.create_dynamic_child(
        vec!["b"].into(),
        "persistent_coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [c:1] storage
    test.create_static_file(&Path::new("b:0/children/persistent_coll:c:0/data/c1"), "hippos")
        .await
        .unwrap();

    // destroy [c:1]
    test.destroy_dynamic_child(vec!["b"].into(), "persistent_coll", "c").await;

    // expect the [c:1] storage and data to persist
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/data",
        vec!["c1".to_string()],
    )
    .await;

    // recreate dynamic child [c:2]
    test.create_dynamic_child(
        vec!["b"].into(),
        "persistent_coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [c:2] storage
    test.create_static_file(&Path::new("b:0/children/persistent_coll:c:0/data/c2"), "sharks")
        .await
        .unwrap();

    // destroy [c:2]
    test.destroy_dynamic_child(vec!["b"].into(), "persistent_coll", "c").await;

    // expect the [c:1] and [c:2] storage and data to persist
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/data",
        vec!["c1".to_string(), "c2".to_string()],
    )
    .await;

    // check that the file can be destroyed by storage admin
    let namespace = test.bind_and_get_namespace(vec!["b"].into()).await;
    let storage_admin_proxy = capability_util::connect_to_svc_in_namespace::<
        fsys::StorageAdminMarker,
    >(
        &namespace, &"/svc/fuchsia.sys2.StorageAdmin".try_into().unwrap()
    )
    .await;
    let _ = storage_admin_proxy
        // the instance ids in the moniker for the request to destroy persistent storage do not matter
        .delete_component_storage("./b:0/persistent_coll:c:0")
        .await
        .unwrap()
        .unwrap();

    // expect persistent_coll storage to be destroyed
    capability_util::confirm_storage_is_deleted_for_component(
        None,
        true,
        InstancedRelativeMoniker::new(vec!["b:0".into(), "persistent_coll:c:0".into()]),
        None,
        &test.test_dir_proxy,
    )
    .await;
}

///   a
///   |
///   b
///   |
///  coll-persistent_storage: "true" / instance_id
///   |
/// [c:1]
///
/// Test that storage data persists after destroy for a collection with an instance-id-based
/// storage path. The persistent storage data can be deleted through a
/// StorageAdminRequest::DeleteComponentStorage request.
/// The following storage paths are used:
///   - indexed path
#[fuchsia::test]
async fn storage_persistence_instance_id_path() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("minfs")
                        .path("/data")
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Capability("data".into()),
                    source_name: "fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    target_name: "fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    target: OfferTarget::static_child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_name: "fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.StorageAdmin".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("persistent_coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_collection(
                    CollectionDeclBuilder::new_transient_collection("persistent_coll")
                        .persistent_storage(true),
                )
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];

    // set instance_id for "b/persistent_coll:c" components
    let instance_id = gen_instance_id(&mut rand::thread_rng());
    let component_id_index_path = make_index_file(component_id_index::Index {
        instances: vec![component_id_index::InstanceIdEntry {
            instance_id: Some(instance_id.clone()),
            appmgr_moniker: None,
            moniker: Some(vec!["b".into(), "persistent_coll:c".into()].into()),
        }],
        ..component_id_index::Index::default()
    })
    .unwrap();

    let test = RoutingTestBuilder::new("a", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    // create [c:1] under the storage persistent collection
    test.create_dynamic_child(
        vec!["b"].into(),
        "persistent_coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [c:1] storage
    test.create_static_file(&Path::new(&format!("{}/c1", instance_id)), "hippos").await.unwrap();

    // destroy [c:1]
    test.destroy_dynamic_child(vec!["b"].into(), "persistent_coll", "c").await;

    // expect the [c:1] storage and data to persist
    test.check_test_subdir_contents(&instance_id, vec!["c1".to_string()]).await;

    // recreate dynamic child [c:2]
    test.create_dynamic_child(
        vec!["b"].into(),
        "persistent_coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [c:2] storage
    test.create_static_file(&Path::new(&format!("{}/c2", instance_id)), "sharks").await.unwrap();

    // destroy [c:2]
    test.destroy_dynamic_child(vec!["b"].into(), "persistent_coll", "c").await;

    // expect the [c:1] and [c:2] storage and data to persist
    test.check_test_subdir_contents(&instance_id, vec!["c1".to_string(), "c2".to_string()]).await;

    // destroy the persistent storage with a storage admin request
    let namespace = test.bind_and_get_namespace(vec!["b"].into()).await;
    let storage_admin_proxy = capability_util::connect_to_svc_in_namespace::<
        fsys::StorageAdminMarker,
    >(
        &namespace, &"/svc/fuchsia.sys2.StorageAdmin".try_into().unwrap()
    )
    .await;
    let _ =
        storage_admin_proxy.delete_component_storage("./b:0/persistent_coll:c:0").await.unwrap();

    // expect persistent_coll storage to be destroyed
    capability_util::confirm_storage_is_deleted_for_component(
        None,
        true,
        InstancedRelativeMoniker::new(vec!["b:0".into(), "persistent_coll:c:0".into()]),
        Some(&instance_id),
        &test.test_dir_proxy,
    )
    .await;
}

///   a
///   |
///   b
///   |
///  coll-persistent_storage: "true" / instance_id
///   |
/// [c:1]
///  / \
/// d  [coll]
///      |
///     [e:1]
///
/// Test that storage persistence behavior is inherited by descendents with a different storage
/// path.
/// The following storage paths are used:
///   - indexed path
///   - moniker path with instance ids cleared
#[fuchsia::test]
async fn storage_persistence_inheritance() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("minfs")
                        .path("/data")
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("persistent_coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_collection(
                    CollectionDeclBuilder::new_transient_collection("persistent_coll")
                        .persistent_storage(true),
                )
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::static_child("d".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("lower_coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_lazy_child("d")
                .add_transient_collection("lower_coll")
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
        (
            "e",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];

    // set instance_id for "b/persistent_coll:c" components
    let instance_id = gen_instance_id(&mut rand::thread_rng());
    let component_id_index_path = make_index_file(component_id_index::Index {
        instances: vec![component_id_index::InstanceIdEntry {
            instance_id: Some(instance_id.clone()),
            appmgr_moniker: None,
            moniker: Some(vec!["b".into(), "persistent_coll:c".into()].into()),
        }],
        ..component_id_index::Index::default()
    })
    .unwrap();

    let test = RoutingTestBuilder::new("a", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    // create [c:1] under the storage persistent collection
    test.create_dynamic_child(
        vec!["b"].into(),
        "persistent_coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [c:1] storage
    test.check_use(
        vec!["b", "persistent_coll:c"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "persistent_coll:c:1".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: None,
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;

    // start d:0 and write to storage
    test.check_use(
        vec!["b", "persistent_coll:c", "d"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "persistent_coll:c:1".into(),
                "d:0".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: None,
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;

    // create [e:1] under the lower collection
    test.create_dynamic_child(
        vec!["b", "persistent_coll:c"].into(),
        "lower_coll",
        ChildDecl {
            name: "e".into(),
            url: "test:///e".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [e:1] storage
    test.check_use(
        vec!["b", "persistent_coll:c", "lower_coll:e"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "persistent_coll:c:1".into(),
                "lower_coll:e:1".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: None,
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;

    // test that [c:1] wrote to instance id path
    test.check_test_subdir_contents(&instance_id, vec!["hippos".to_string()]).await;
    // test that d:0 wrote to moniker based path with instance ids cleared
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/children/d:0/data",
        vec!["hippos".to_string()],
    )
    .await;
    // test that [e:1] wrote to moniker based path with instance ids cleared
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/children/lower_coll:e:0/data",
        vec!["hippos".to_string()],
    )
    .await;

    // destroy [c:1], which will also shutdown d:0 and lower_coll:e:1
    test.destroy_dynamic_child(vec!["b"].into(), "persistent_coll", "c").await;

    // expect [c:1], d:0, and [e:1] storage and data to persist
    test.check_test_subdir_contents(&instance_id, vec!["hippos".to_string()]).await;
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/children/d:0/data",
        vec!["hippos".to_string()],
    )
    .await;
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/children/lower_coll:e:0/data",
        vec!["hippos".to_string()],
    )
    .await;
}

///    a
///    |
///    b
///    |
///   coll-persistent_storage: "true" / instance_id
///   |
///  [c:1]
///   / \
///  d  coll-persistent_storage: "false"
///      |
///     [e:1]
///
///  Test that storage persistence can be disabled by a lower-level collection.
///  The following storage paths are used:
///   - indexed path
///   - moniker path with instance ids cleared
///   - moniker path with instance ids visible
#[fuchsia::test]
async fn storage_persistence_disablement() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("minfs")
                        .path("/data")
                        .rights(*routing::rights::READ_RIGHTS | *routing::rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "data".into(),
                    backing_dir: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                    storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Self_,
                    target: OfferTarget::static_child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("persistent_coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_collection(
                    CollectionDeclBuilder::new_transient_collection("persistent_coll")
                        .persistent_storage(true),
                )
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::static_child("d".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferSource::Parent,
                    target: OfferTarget::Collection("non_persistent_coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                    availability: Availability::Required,
                }))
                .add_lazy_child("d")
                .add_collection(
                    CollectionDeclBuilder::new_transient_collection("non_persistent_coll")
                        .persistent_storage(false),
                )
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_name: "fuchsia.component.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.component.Realm".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                    availability: Availability::Required,
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
        (
            "e",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/data".try_into().unwrap(),
                    availability: Availability::Required,
                }))
                .build(),
        ),
    ];

    // set instance_id for "b/persistent_coll:c" components
    let instance_id = gen_instance_id(&mut rand::thread_rng());
    let component_id_index_path = make_index_file(component_id_index::Index {
        instances: vec![component_id_index::InstanceIdEntry {
            instance_id: Some(instance_id.clone()),
            appmgr_moniker: None,
            moniker: Some(vec!["b".into(), "persistent_coll:c".into()].into()),
        }],
        ..component_id_index::Index::default()
    })
    .unwrap();

    let test = RoutingTestBuilder::new("a", components)
        .set_component_id_index_path(component_id_index_path.path().to_str().unwrap().to_string())
        .build()
        .await;

    // create [c:1] under the storage persistent collection
    test.create_dynamic_child(
        vec!["b"].into(),
        "persistent_coll",
        ChildDecl {
            name: "c".into(),
            url: "test:///c".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [c:1] storage
    test.check_use(
        vec!["b", "persistent_coll:c"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "persistent_coll:c:1".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: None,
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;

    // start d:0 and write to storage
    test.check_use(
        vec!["b", "persistent_coll:c", "d"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "persistent_coll:c:1".into(),
                "d:0".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: None,
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;

    // create [e:1] under the non persistent collection
    test.create_dynamic_child(
        vec!["b", "persistent_coll:c"].into(),
        "non_persistent_coll",
        ChildDecl {
            name: "e".into(),
            url: "test:///e".to_string(),
            startup: fdecl::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        },
    )
    .await;

    // write to [e:1] storage
    test.check_use(
        vec!["b", "persistent_coll:c", "non_persistent_coll:e"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            storage_relation: Some(InstancedRelativeMoniker::new(vec![
                "b:0".into(),
                "persistent_coll:c:1".into(),
                "non_persistent_coll:e:1".into(),
            ])),
            from_cm_namespace: false,
            storage_subdir: None,
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;

    // test that [c:1] wrote to instance id path
    test.check_test_subdir_contents(&instance_id, vec!["hippos".to_string()]).await;
    // test that b:0 children includes:
    // 1. persistent_coll:c:0 used by persistent storage
    // 2. persistent_coll:c:1 used by non persistent storage
    test.check_test_subdir_contents(
        "b:0/children",
        vec!["persistent_coll:c:0".to_string(), "persistent_coll:c:1".to_string()],
    )
    .await;
    // test that d:0 wrote to moniker based path with instance ids cleared
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/children/d:0/data",
        vec!["hippos".to_string()],
    )
    .await;
    // test that [e:1] wrote to moniker based path with all instance ids visible
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:1/children/non_persistent_coll:e:1/data",
        vec!["hippos".to_string()],
    )
    .await;

    // destroy [c:1], which will shutdown d:0 and destroy [e:1]
    test.destroy_dynamic_child(vec!["b"].into(), "persistent_coll", "c").await;

    // expect [c:1], d:0 storage and data to persist
    test.check_test_subdir_contents(&instance_id, vec!["hippos".to_string()]).await;
    test.check_test_subdir_contents(
        "b:0/children/persistent_coll:c:0/children/d:0/data",
        vec!["hippos".to_string()],
    )
    .await;

    // expect non_persistent_coll storage and data to be destroyed (only persistent_coll exists)
    capability_util::confirm_storage_is_deleted_for_component(
        None,
        false,
        InstancedRelativeMoniker::new(vec![
            "b:0".into(),
            "persistent_coll:c:1".into(),
            "non_persistent_coll:e:1".into(),
        ]),
        None,
        &test.test_dir_proxy,
    )
    .await;
}
