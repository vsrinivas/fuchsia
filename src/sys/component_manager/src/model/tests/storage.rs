// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::moniker::RelativeMoniker,
    crate::model::testing::{routing_test_helpers::*, test_helpers::*},
    cm_rust::*,
    fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
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
                    source: StorageDirectorySource::Self_,
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
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into()])),
        },
    )
    .await;
}

///   a
///    \
///     b
///
/// a: has storage decl with name "mystorage" with a source of self at path /data
/// a: offers meta storage to b from "mystorage"
/// b: uses meta storage
#[fuchsia_async::run_singlethreaded(test)]
async fn meta_storage_and_dir_from_parent() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
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
                    source: StorageDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::Storage(UseStorageDecl::Meta)],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into()])),
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
            ComponentDecl {
                offers: vec![OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "/data".try_into().unwrap(),
                    target_path: "/minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
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
                    source: StorageDirectorySource::Realm,
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
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
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
                    source: StorageDirectorySource::Self_,
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
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into(), "c:0".into()])),
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
/// a: offers meta storage to b from "mystorage"
/// b: offers meta storage to c from realm
/// c: uses meta storage
#[fuchsia_async::run_singlethreaded(test)]
async fn meta_storage_and_dir_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
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
                    source: StorageDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                offers: vec![OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
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
                uses: vec![UseDecl::Storage(UseStorageDecl::Meta)],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["b:0".into(), "c:0".into()])),
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
            ComponentDecl {
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Child("b".to_string()),
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
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
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
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
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
#[ignore] // TODO(dgonyeo): reenable this test
async fn use_in_collection_from_parent() {
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "/data".try_into().unwrap(),
                    target_path: "/minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
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
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                })],
                offers: vec![
                    OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Collection("coll".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Cache(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Collection("coll".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Collection("coll".to_string()),
                    })),
                ],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Realm,
                }],
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Storage(UseStorageDecl::Data("/data".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Cache("/cache".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Meta),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_dynamic_child(
        vec!["b:0"].into(),
        "coll",
        ChildDecl {
            name: "c".to_string(),
            url: "test:///c".to_string(),
            startup: fsys::StartupMode::Lazy,
        },
    )
    .await;

    // Use storage and confirm its existence.
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["coll:c:1".into()])),
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["coll:c:1".into()])),
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/unused".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["coll:c:1".into()])),
        },
    )
    .await;
    // "foo" is here since it's automatically created by the test.
    assert_eq!(
        test.list_directory_in_storage(RelativeMoniker::new(vec![], vec![]), "").await,
        vec!["coll:c:1".to_string(), "foo".to_string()],
    );
    test.destroy_dynamic_child(vec!["b:0"].into(), "coll", "c", 1).await;

    // Confirm storage no longer exists.
    assert_eq!(
        test.list_directory_in_storage(RelativeMoniker::new(vec![], vec![]), "").await,
        vec!["foo".to_string()],
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
            ComponentDecl {
                offers: vec![
                    OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Child("b".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Cache(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Child("b".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Child("b".to_string()),
                    })),
                ],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.Realm".try_into().unwrap(),
                })],
                offers: vec![
                    OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                        source: OfferStorageSource::Realm,
                        target: OfferTarget::Collection("coll".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Cache(OfferStorage {
                        source: OfferStorageSource::Realm,
                        target: OfferTarget::Collection("coll".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
                        source: OfferStorageSource::Realm,
                        target: OfferTarget::Collection("coll".to_string()),
                    })),
                ],
                collections: vec![CollectionDecl {
                    name: "coll".to_string(),
                    durability: fsys::Durability::Transient,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Storage(UseStorageDecl::Data("/data".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Cache("/cache".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Meta),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_dynamic_child(
        vec!["b:0"].into(),
        "coll",
        ChildDecl {
            name: "c".to_string(),
            url: "test:///c".to_string(),
            startup: fsys::StartupMode::Lazy,
        },
    )
    .await;

    // Use storage and confirm its existence.
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/data".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(
                vec![],
                vec!["b:0".into(), "coll:c:1".into()],
            )),
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/cache".try_into().unwrap(),
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(
                vec![],
                vec!["b:0".into(), "coll:c:1".into()],
            )),
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Storage {
            path: "/unused".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: Some(RelativeMoniker::new(
                vec![],
                vec!["b:0".into(), "coll:c:1".into()],
            )),
        },
    )
    .await;
    assert_eq!(
        test.list_directory_in_storage(
            RelativeMoniker::new(vec![], vec!["b:0".into()]),
            "children",
        )
        .await,
        vec!["coll:c:1".to_string()]
    );
    test.destroy_dynamic_child(vec!["b:0"].into(), "coll", "c", 1).await;

    // Confirm storage no longer exists.
    assert_eq!(
        test.list_directory_in_storage(
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
            ComponentDecl {
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Child("b".to_string()),
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
                    OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
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
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                })],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                offers: vec![
                    OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                        source: OfferStorageSource::Realm,
                        target: OfferTarget::Child("d".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
                        source: OfferStorageSource::Realm,
                        target: OfferTarget::Child("d".to_string()),
                    })),
                ],
                uses: vec![
                    UseDecl::Storage(UseStorageDecl::Cache("/storage".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Meta),
                ],
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
                uses: vec![
                    UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Meta),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Cache,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
        },
    )
    .await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Storage {
            path: "/unused".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into()])),
        },
    )
    .await;
    test.check_use(
        vec!["c:0", "d:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into(), "d:0".into()])),
        },
    )
    .await;
    test.check_use(
        vec!["c:0", "d:0"].into(),
        CheckUse::Storage {
            path: "/unused".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: Some(RelativeMoniker::new(vec![], vec!["c:0".into(), "d:0".into()])),
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
                    source: StorageDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![
                    UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Meta),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: None,
        },
    )
    .await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: None,
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
            ComponentDecl {
                offers: vec![OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "/data".try_into().unwrap(),
                    target_path: "/data".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
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
                uses: vec![UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap()))],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: None,
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
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/data".try_into().unwrap(),
                    source: StorageDirectorySource::Self_,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![
                    UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Meta),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Cache,
            storage_relation: None,
        },
    )
    .await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: None,
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
            ComponentDecl {
                program: None,
                offers: vec![OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: "/data".try_into().unwrap(),
                    target_path: "/minfs".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
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
                offers: vec![
                    OfferDecl::Storage(OfferStorageDecl::Data(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Child("c".to_string()),
                    })),
                    OfferDecl::Storage(OfferStorageDecl::Meta(OfferStorage {
                        source: OfferStorageSource::Storage("mystorage".to_string()),
                        target: OfferTarget::Child("c".to_string()),
                    })),
                ],
                children: vec![ChildDecl {
                    name: "c".to_string(),
                    url: "test:///c".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                storage: vec![StorageDecl {
                    name: "mystorage".to_string(),
                    source_path: "/minfs".try_into().unwrap(),
                    source: StorageDirectorySource::Realm,
                }],
                ..default_component_decl()
            },
        ),
        (
            "c",
            ComponentDecl {
                uses: vec![
                    UseDecl::Storage(UseStorageDecl::Data("/storage".try_into().unwrap())),
                    UseDecl::Storage(UseStorageDecl::Meta),
                ],
                ..default_component_decl()
            },
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Data,
            storage_relation: None,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Storage {
            path: "/storage".try_into().unwrap(),
            type_: fsys::StorageType::Meta,
            storage_relation: None,
        },
    )
    .await;
}
