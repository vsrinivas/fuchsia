// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        moniker::RelativeMoniker,
        rights,
        testing::{routing_test_helpers::*, test_helpers::*},
    },
    cm_rust::*,
    fidl_fuchsia_sys2 as fsys,
    std::{convert::TryInto, fs, path::PathBuf},
};

///   component manager's namespace
///    |
///    a
///    |
///    b
///
/// a: has storage decl with name "mystorage" with a source of realm at path /data
/// a: offers cache storage to b from "mystorage"
/// b: uses cache storage as /storage.
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_dir_from_cm_namespace() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "/tmp".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some(PathBuf::from("cache")),
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into()])),
            from_cm_namespace: true,
            storage_subdir: Some("cache".to_string()),
        },
    )
    .await;
    let tmp_cache_entries =
        fs::read_dir("/tmp/cache").unwrap().map(|e| e.unwrap().path()).collect::<Vec<PathBuf>>();
    assert_eq!(tmp_cache_entries, vec![PathBuf::from("/tmp/cache/b:0")]);
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /data
/// a: offers cache storage to b from "mystorage"
/// b: uses cache storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_and_dir_from_parent() {
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
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into()])),
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
    assert_eq!(test.list_directory(".").await, vec!["b:0".to_string(), "foo".to_string()],);
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /data, with subdir
///    "cache"
/// a: offers cache storage to b from "mystorage"
/// b: uses cache storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_and_dir_from_parent_with_subdir() {
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
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: Some(PathBuf::from("cache")),
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("cache".to_string()),
        },
    )
    .await;
    assert_eq!(test.list_directory(".").await, vec!["cache".to_string(), "foo".to_string()],);
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /data
/// a: offers cache storage to b from "mystorage"
/// b: uses cache storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_and_dir_from_parent_legacy_path_based() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "/data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into()])),
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /data, but /data
///    has only read rights
/// a: offers cache storage to b from "mystorage"
/// b: uses cache storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_and_dir_from_parent_rights_invalid() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS)
                        .build(),
                )
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: None,
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data to b as /minfs
/// b: has storage decl with name "mystorage" with a source of realm at path /minfs
/// b: offers data storage to c from "mystorage"
/// c: uses data storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_grandparent() {
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
                    source: OfferDirectorySource::Self_,
                    source_path: "data".try_into().unwrap(),
                    target_path: "minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("c")
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: None,
                })
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data to b as /minfs with subdir "subdir_1"
/// b: has storage decl with name "mystorage" with a source of realm at path /minfs with subdir
///    "subdir_2"
/// b: offers data storage to c from "mystorage"
/// c: uses data storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_grandparent_with_subdirs() {
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
                    source: OfferDirectorySource::Self_,
                    source_path: "data".try_into().unwrap(),
                    target_path: "minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: Some("subdir_1".into()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("c")
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some("subdir_2".into()),
                })
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.add_subdir_to_data_directory("subdir_1");
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("subdir_1/subdir_2".to_string()),
        },
    )
    .await;
    assert_eq!(test.list_directory(".").await, vec!["foo".to_string(), "subdir_1".to_string()]);
    assert_eq!(test.list_directory("subdir_1").await, vec!["subdir_2".to_string()]);
    assert_eq!(test.list_directory("subdir_1/subdir_2").await, vec!["c:0".to_string()]);
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data to b as /minfs
/// b: has storage decl with name "mystorage" with a source of realm at path /minfs, subdir "bar"
/// b: offers data storage to c from "mystorage"
/// c: uses data storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_grandparent_with_subdir() {
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
                    source: OfferDirectorySource::Self_,
                    source_path: "data".try_into().unwrap(),
                    target_path: "minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("c")
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some("bar".try_into().unwrap()),
                })
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("bar".to_string()),
        },
    )
    .await;
    assert_eq!(test.list_directory(".").await, vec!["bar".to_string(), "foo".to_string()],);
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data to b as /minfs
/// b: has storage decl with name "mystorage" with a source of realm at path /minfs
/// b: offers data storage to c from "mystorage"
/// c: uses data storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_grandparent_legacy_path_based() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "/data".try_into().unwrap(),
                    target_path: "/minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("c")
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: None,
                })
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// a: has storage decl with name "mystorage" with a source of self at path /data
/// a: offers data storage to b from "mystorage"
/// b: offers data storage to c from realm
/// c: uses data storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_and_dir_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data-root")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "data-root".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Parent,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into(), "c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///  / \
/// b   c
///
/// b: exposes directory /data as /minfs
/// a: has storage decl with name "mystorage" with a source of child b at path /minfs
/// a: offers cache storage to c from "mystorage"
/// c: uses cache storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_sibling() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Child("b".to_string()),
                    subdir: None,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: "data".try_into().unwrap(),
                    source: ExposeSource::Self_,
                    target_path: "minfs".try_into().unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///  / \
/// b   c
///
/// b: exposes directory /data as /minfs with subdir "subdir_1"
/// a: has storage decl with name "mystorage" with a source of child b at path /minfs and subdir
///    "subdir_2"
/// a: offers cache storage to c from "mystorage"
/// c: uses cache storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_sibling_with_subdir() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Child("b".to_string()),
                    subdir: Some("subdir_2".into()),
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: "data".try_into().unwrap(),
                    source: ExposeSource::Self_,
                    target_path: "minfs".try_into().unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: Some("subdir_1".into()),
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.add_subdir_to_data_directory("subdir_1");
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("subdir_1/subdir_2".to_string()),
        },
    )
    .await;
    assert_eq!(test.list_directory(".").await, vec!["foo".to_string(), "subdir_1".to_string()]);
    assert_eq!(test.list_directory("subdir_1").await, vec!["subdir_2".to_string()]);
    assert_eq!(test.list_directory("subdir_1/subdir_2").await, vec!["c:0".to_string()]);
}

///   a
///  / \
/// b   c
///
/// b: exposes directory /data as /minfs
/// a: has storage decl with name "mystorage" with a source of child b at path /minfs
/// a: offers cache storage to c from "mystorage"
/// c: uses cache storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_sibling_legacy_path_based() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Child("b".to_string()),
                    subdir: None,
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: "/data".try_into().unwrap(),
                    source: ExposeSource::Self_,
                    target_path: "/minfs".try_into().unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
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
#[fuchsia_async::run_singlethreaded(test)]
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
                    source: OfferDirectorySource::Self_,
                    source_path: "data".try_into().unwrap(),
                    target_path: "minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                    source_path: "fuchsia.sys2.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some(PathBuf::from("data")),
                })
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: Some(PathBuf::from("cache")),
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
        },
    )
    .await;
    // Confirm storage directory exists for component in collection
    assert_eq!(
        test.list_directory_in_storage(Some("data"), RelativeMoniker::new(vec![], vec![]), "")
            .await,
        vec!["coll:c:1".to_string()],
    );
    assert_eq!(
        test.list_directory_in_storage(Some("cache"), RelativeMoniker::new(vec![], vec![]), "")
            .await,
        vec!["coll:c:1".to_string()],
    );
    test.destroy_dynamic_child(vec!["b:0"].into(), "coll", "c").await;

    // Confirm storage no longer exists.
    assert_eq!(
        test.list_directory_in_storage(Some("data"), RelativeMoniker::new(vec![], vec![]), "")
            .await,
        Vec::<String>::new(),
    );
    assert_eq!(
        test.list_directory_in_storage(Some("cache"), RelativeMoniker::new(vec![], vec![]), "")
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
#[fuchsia_async::run_singlethreaded(test)]
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
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: Some(PathBuf::from("data")),
                })
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: Some(PathBuf::from("cache")),
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: "fuchsia.sys2.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Parent,
                    target: OfferTarget::Collection("coll".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Parent,
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
        },
    )
    .await;
    assert_eq!(
        test.list_directory_in_storage(
            Some("data"),
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
            "children",
        )
        .await,
        vec!["coll:c:1".to_string()]
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
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
            "children"
        )
        .await,
        Vec::<String>::new(),
    );
    assert_eq!(
        test.list_directory_in_storage(
            Some("cache"),
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
            "children"
        )
        .await,
        Vec::<String>::new(),
    );
}

///   a
///  / \
/// b   c
///      \
///       d
///
/// b: exposes directory /data as /minfs
/// a: has storage decl with name "mystorage" with a source of child b at path /minfs
/// a: offers data, cache, and meta storage to c from "mystorage"
/// c: uses cache and meta storage as /storage
/// c: offers data and meta storage to d
/// d: uses data and meta storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_multiple_types() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Child("b".to_string()),
                    subdir: Some(PathBuf::from("data")),
                })
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Child("b".to_string()),
                    subdir: Some(PathBuf::from("cache")),
                })
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: "data".try_into().unwrap(),
                    source: ExposeSource::Self_,
                    target_path: "minfs".try_into().unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Parent,
                    target: OfferTarget::Child("d".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Parent,
                    target: OfferTarget::Child("d".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/cache".try_into().unwrap(),
                }))
                .add_lazy_child("d")
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "cache".into(),
                    target_path: "/cache".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("data".to_string()),
        },
    )
    .await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("cache".to_string()),
        },
    )
    .await;
    test.check_use(
        vec!["c:0", "d:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into(), "d:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("data".to_string()),
        },
    )
    .await;
    test.check_use(
        vec!["c:0", "d:0"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into(), "d:0".into()])),
            from_cm_namespace: false,
            storage_subdir: Some("cache".to_string()),
        },
    )
    .await;
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /storage
/// a: offers cache storage to b from "mystorage"
/// b: uses data storage as /storage, fails to since data != cache
/// b: uses meta storage, fails to since meta != cache
#[fuchsia_async::run_singlethreaded(test)]
async fn use_the_wrong_type_of_storage() {
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
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("b".to_string()),
                    source_name: "cache".into(),
                    target_name: "cache".into(),
                }))
                .add_lazy_child("b")
                .storage(StorageDecl {
                    name: "cache".into(),
                    source_path: "data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: None,
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///    \
///     b
///
/// a: offers directory from self at path "/data"
/// b: uses data storage as /storage, fails to since data storage != "/data" directories
#[fuchsia_async::run_singlethreaded(test)]
async fn directories_are_not_storage() {
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
                    source: OfferDirectorySource::Self_,
                    source_path: "data".try_into().unwrap(),
                    target_path: "data".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: None,
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /data
/// a: does not offer any storage to b
/// b: uses meta storage and data storage as /storage, fails to since it was not offered either
#[fuchsia_async::run_singlethreaded(test)]
async fn use_storage_when_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .directory(
                    DirectoryDeclBuilder::new("minfs")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                    subdir: None,
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: None,
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data to b as /minfs, but a is non-executable
/// b: has storage decl with name "mystorage" with a source of realm at path /minfs
/// b: offers data and meta storage to b from "mystorage"
/// c: uses meta and data storage as /storage, fails to since a is non-executable
#[fuchsia_async::run_singlethreaded(test)]
async fn dir_offered_from_nonexecutable() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new_empty_component()
                .directory(
                    DirectoryDeclBuilder::new("data")
                        .path("/data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "data".try_into().unwrap(),
                    target_path: "minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                .offer(OfferDecl::Storage(OfferStorageDecl {
                    source: OfferStorageSource::Self_,
                    target: OfferTarget::Child("c".to_string()),
                    source_name: "data".into(),
                    target_name: "data".into(),
                }))
                .add_lazy_child("c")
                .storage(StorageDecl {
                    name: "data".into(),
                    source_path: "minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Parent,
                    subdir: None,
                })
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Storage(UseStorageDecl {
                    source_name: "data".into(),
                    target_path: "/storage".try_into().unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            storage_relation: None,
            from_cm_namespace: false,
            storage_subdir: None,
        },
    )
    .await;
}
