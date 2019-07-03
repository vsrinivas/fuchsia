// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::moniker::RelativeMoniker,
    crate::model::testing::mocks::*,
    crate::model::testing::routing_test_helpers::*,
    cm_rust::{
        self, ChildDecl, ComponentDecl, ExposeDecl, ExposeDirectoryDecl, ExposeSource, OfferDecl,
        OfferDirectoryDecl, OfferDirectorySource, OfferStorage, OfferStorageDecl,
        OfferStorageSource, OfferTarget, StorageDecl, UseDecl, UseStorageDecl,
    },
    fidl_fuchsia_sys2 as fsys,
    std::convert::TryInto,
};

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
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Cache(OfferStorage {
                    source: OfferStorageSource::Storage("mystorage".to_string()),
                    target: OfferTarget::Child("b".to_string()),
                }))],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/data".try_into().unwrap(),
                    source: OfferDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Cache("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["b"].into(),
        CheckUse::Storage {
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b".into()])),
        }
    ));
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data to b as /minfs
/// b: has storage decl with name "mystorage" with a source of realm at path /minfs
/// b: offers data storage to b from "mystorage"
/// c: uses data storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_from_parent_dir_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "/data".try_into().unwrap(),
                    target_path: "/minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                })],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                    source: OfferStorageSource::Storage("mystorage".to_string()),
                    target: OfferTarget::Child("c".to_string()),
                }))],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: OfferDirectorySource::Realm,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["b", "c"].into(),
        CheckUse::Storage {
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c".into()])),
        }
    ));
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
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                    source: OfferStorageSource::Storage("mystorage".to_string()),
                    target: OfferTarget::Child("b".to_string()),
                }))],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/data".try_into().unwrap(),
                    source: OfferDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                    source: OfferStorageSource::Realm,
                    target: OfferTarget::Child("c".to_string()),
                }))],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["b", "c"].into(),
        CheckUse::Storage {
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b".into(), "c".into()])),
        }
    ));
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
            ComponentDecl {
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: OfferDirectorySource::Child("b".to_string()),
                }],
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Cache(OfferStorage {
                    source: OfferStorageSource::Storage("mystorage".to_string()),
                    target: OfferTarget::Child("c".to_string()),
                }))],
                children: vec![
                    ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                exposes: vec![ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: "/data".try_into().unwrap(),
                    source: ExposeSource::Self_,
                    target_path: "/minfs".try_into().unwrap(),
                })],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Cache("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["c"].into(),
        CheckUse::Storage {
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c".into()])),
        }
    ));
}

///   a
///  / \
/// b   c
///      \
///       d
///
/// b: exposes directory /data as /minfs
/// a: has storage decl with name "mystorage" with a source of child b at path /minfs
/// a: offers data and cache storage to c from "mystorage"
/// c: uses cache storage as /storage
/// c: offers data storage to d
/// d: uses data storage
#[fuchsia_async::run_singlethreaded(test)]
async fn storage_multiple_types() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: OfferDirectorySource::Child("b".to_string()),
                }],
                offers: vec![
                    OfferDecl::Storage(OfferStorageDecl::Cache(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Child("c".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Child("c".to_string()),
                    })),
                ],
                children: vec![
                    ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                    ChildDecl {
                        name: "c".to_string(),
                        url: "test:///c".to_string(),
                        startup: fsys::StartupMode::Lazy,
                    },
                ],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                exposes: vec![ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: "/data".try_into().unwrap(),
                    source: ExposeSource::Self_,
                    target_path: "/minfs".try_into().unwrap(),
                })],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                    source: OfferStorageSource::Realm,
                    target: OfferTarget::Child("d".to_string()),
                }))],
                uses: vec![UseDecl::Storage(UseStorageDecl::Cache("/storage".try_into().unwrap()))],
                children: vec![ChildDecl {
                    name: "d".to_string(),
                    url: "test:///d".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "d",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["c"].into(),
        CheckUse::Storage {
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c".into()])),
        }
    ));
    await!(test.check_use(
        vec!["c", "d"].into(),
        CheckUse::Storage {
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c".into(), "d".into()])),
        }
    ));
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /storage
/// a: offers cache storage to b from "mystorage"
/// b: uses data storage as /storage, fails to since data != cache
#[fuchsia_async::run_singlethreaded(test)]
async fn use_the_wrong_type_of_storage() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Cache(OfferStorage {
                    source: OfferStorageSource::Storage("mystorage".to_string()),
                    target: OfferTarget::Child("b".to_string()),
                }))],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/data".try_into().unwrap(),
                    source: OfferDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["b"].into(),
        CheckUse::Storage { type_: fsys::StorageType::Cache, storage_relation: None }
    ));
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /data
/// a: does not offer any storage to b
/// b: uses data storage as /storage, fails to since it was not offered
#[fuchsia_async::run_singlethreaded(test)]
async fn use_storage_when_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/data".try_into().unwrap(),
                    source: OfferDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["b"].into(),
        CheckUse::Storage { type_: fsys::StorageType::Cache, storage_relation: None }
    ));
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data to b as /minfs, but a is non-executable
/// b: has storage decl with name "mystorage" with a source of realm at path /minfs
/// b: offers data storage to b from "mystorage"
/// c: uses data storage as /storage
#[fuchsia_async::run_singlethreaded(test)]
async fn dir_offered_from_nonexecutable() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                program: None,
                offers: vec![OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "/data".try_into().unwrap(),
                    target_path: "/minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                })],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                    source: OfferStorageSource::Storage("mystorage".to_string()),
                    target: OfferTarget::Child("c".to_string()),
                }))],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: OfferDirectorySource::Realm,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let ambient = Box::new(MockAmbientEnvironment::new());
    let test = RoutingTest::new("a", components, ambient);
    await!(test.check_use(
        vec!["b", "c"].into(),
        CheckUse::Storage { type_: fsys::StorageType::Data, storage_relation: None }
    ));
}
