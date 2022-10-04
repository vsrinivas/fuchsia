// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{CheckUse, ExpectedResult, RoutingTestModel, RoutingTestModelBuilder},
    cm_moniker::InstancedRelativeMoniker,
    cm_rust::*,
    cm_rust_testing::*,
    fidl_fuchsia_component_decl as fdecl,
    moniker::RelativeMonikerBase,
    routing::rights::{READ_RIGHTS, WRITE_RIGHTS},
    std::{
        convert::{TryFrom, TryInto},
        marker::PhantomData,
        path::PathBuf,
    },
};

pub struct CommonStorageAdminTest<T: RoutingTestModelBuilder> {
    builder: PhantomData<T>,
}

impl<T: RoutingTestModelBuilder> CommonStorageAdminTest<T> {
    pub fn new() -> Self {
        Self { builder: PhantomData }
    }

    ///    a
    ///   / \
    ///  b   c
    ///
    /// a: has storage decl with name "data" with a source of self at path /data
    /// a: offers data storage to b
    /// a: offers a storage admin protocol to c from the "data" storage capability
    /// b: uses data storage as /storage.
    /// c: uses the storage admin protocol to access b's storage
    pub async fn test_storage_to_one_child_admin_to_another(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
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
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: OfferTarget::static_child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Storage(UseStorageDecl {
                        source_name: "data".into(),
                        target_path: "/storage".try_into().unwrap(),
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["c"].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["b:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///    a
    ///    |
    ///    b
    ///    |
    ///    c
    ///
    /// a: has directory decl with name "data" with a source of self at path /data subdir "foo"
    /// a: offers data to b
    /// b: has storage decl with name "storage" based on "data" from parent subdir "bar"
    /// b: offers a storage admin protocol to c from the "storage" storage capability
    /// c: uses the storage admin protocol to access its own storage
    pub async fn test_directory_from_grandparent_storage_and_admin_from_parent(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("data")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Self_,
                        source_name: "data".into(),
                        target_name: "data".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        rights: Some(*READ_RIGHTS | *WRITE_RIGHTS),
                        subdir: Some(PathBuf::from("foo")),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .storage(StorageDecl {
                        name: "storage".into(),
                        backing_dir: "data".try_into().unwrap(),
                        source: StorageDirectorySource::Parent,
                        subdir: Some(PathBuf::from("bar")),
                        storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                    })
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Capability("storage".into()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: OfferTarget::static_child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b", "c"].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["c:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: Some("foo/bar".to_string()),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///    a
    ///   / \
    ///  b   c
    ///      |
    ///      d
    ///
    /// c: has storage decl with name "data" with a source of self at path /data
    /// c: has storage admin protocol from the "data" storage admin capability
    /// c: offers data storage to d
    /// d: uses data storage
    /// a: offers storage admin protocol from c to b
    /// b: uses the storage admin protocol
    pub async fn test_storage_admin_from_sibling(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("c".to_string()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
                        source: StorageDirectorySource::Self_,
                        subdir: None,
                        storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                    })
                    .offer(OfferDecl::Storage(OfferStorageDecl {
                        source: OfferSource::Self_,
                        target: OfferTarget::static_child("d".to_string()),
                        source_name: "data".into(),
                        target_name: "data".into(),
                        availability: Availability::Required,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Capability("data".into()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: ExposeTarget::Parent,
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
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let test = T::new("a", components).build().await;
        test.check_use(
            vec!["b"].into(),
            CheckUse::StorageAdmin {
                storage_relation: InstancedRelativeMoniker::new(vec!["d:0".into()]),
                from_cm_namespace: false,
                storage_subdir: None,
                expected_res: ExpectedResult::Ok,
            },
        )
        .await;
    }

    ///    a
    ///    |
    ///    b
    ///
    /// a: has storage decl with name "data" with a source of self at path /data
    /// a: offers data storage to b
    /// a: uses a storage admin protocol from #data
    /// b: uses data storage as /storage.
    pub async fn test_admin_protocol_used_in_the_same_place_storage_is_declared(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
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
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Capability("data".into()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
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
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["b:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///    a
    ///    |
    ///    b
    ///
    /// a: has storage decl with name "data" with a source of self at path /data
    /// a: declares a protocol "unrelated.protocol"
    /// a: offers data storage to b
    /// a: uses a storage admin protocol from "unrelated.protocol"
    /// b: uses data storage as /storage.
    pub async fn test_storage_admin_from_protocol_on_self(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .protocol(ProtocolDeclBuilder::new("unrelated.protocol").build())
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
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
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Capability("unrelated.protocol".into()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
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
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["b:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::ErrWithNoEpitaph,
                },
            )
            .await;
    }

    ///    a
    ///    |
    ///    b
    ///
    /// a: has storage decl with name "data" with a source of self at path /data
    /// a: declares a protocol "unrelated.protocol"
    /// a: offers a storage admin protocol from "unrelated.protocol" to b
    /// b: uses storage admin protocol
    pub async fn test_storage_admin_from_protocol_from_parent(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .protocol(ProtocolDeclBuilder::new("unrelated.protocol").build())
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
                        source: StorageDirectorySource::Self_,
                        subdir: None,
                        storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                    })
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Capability("unrelated.protocol".into()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
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
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["b:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::ErrWithNoEpitaph,
                },
            )
            .await;
    }

    ///    a
    ///   / \
    ///  b   c
    ///      |
    ///      d
    ///
    /// c: has storage decl with name "data" with a source of self at path /data
    /// c: has protocol decl with name "unrelated.protocol"
    /// c: has storage admin protocol from the "unrelated.protocol" capability
    /// c: offers data storage to d
    /// d: uses data storage
    /// a: offers storage admin protocol from c to b
    /// b: uses the storage admin protocol
    pub async fn test_storage_admin_from_protocol_on_sibling(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("c".to_string()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
                        source: StorageDirectorySource::Self_,
                        subdir: None,
                        storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                    })
                    .protocol(ProtocolDeclBuilder::new("unrelated.protocol").build())
                    .offer(OfferDecl::Storage(OfferStorageDecl {
                        source: OfferSource::Self_,
                        target: OfferTarget::static_child("d".to_string()),
                        source_name: "data".into(),
                        target_name: "data".into(),
                        availability: Availability::Required,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Capability("unrelated.protocol".into()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: ExposeTarget::Parent,
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
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["d:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::ErrWithNoEpitaph,
                },
            )
            .await;
    }

    ///    a
    ///    |
    ///    b
    ///
    /// a: has storage decl with name "data" with a source of self at path /data
    /// a: offers data storage to b
    /// a: uses a "unrelated.protocol" protocol from "data"
    /// b: uses data storage as /storage.
    pub async fn test_storage_admin_from_storage_on_self_bad_protocol_name(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .protocol(ProtocolDeclBuilder::new("unrelated.protocol").build())
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
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
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Capability("unrelated.protocol".into()),
                        source_name: "unrelated.protocol".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
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
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["b:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::ErrWithNoEpitaph,
                },
            )
            .await;
    }

    ///    a
    ///    |
    ///    b
    ///
    /// a: has storage decl with name "data" with a source of self at path /data
    /// a: offers a storage admin protocol from "data" to b with a source name of "unrelated.protocol"
    /// b: uses storage admin protocol
    pub async fn test_storage_admin_from_storage_on_parent_bad_protocol_name(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
                        source: StorageDirectorySource::Self_,
                        subdir: None,
                        storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                    })
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Capability("data".into()),
                        source_name: "unrelated.protocol".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
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
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["b:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::ErrWithNoEpitaph,
                },
            )
            .await;
    }

    ///    a
    ///   / \
    ///  b   c
    ///      |
    ///      d
    ///
    /// c: has storage decl with name "data" with a source of self at path /data
    /// c: exposes storage admin protocol from "data" with a source name of "unrelated.protocol"
    /// c: offers data storage to d
    /// d: uses data storage
    /// a: offers storage admin protocol from c to b
    /// b: uses the storage admin protocol
    pub async fn test_storage_admin_from_protocol_on_sibling_bad_protocol_name(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::static_child("c".to_string()),
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: OfferTarget::static_child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.StorageAdmin".into(),
                        target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.StorageAdmin")
                            .unwrap(),
                        dependency_type: DependencyType::Strong,
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(
                        DirectoryDeclBuilder::new("tmpfs")
                            .path("/data")
                            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                            .build(),
                    )
                    .storage(StorageDecl {
                        name: "data".into(),
                        backing_dir: "tmpfs".try_into().unwrap(),
                        source: StorageDirectorySource::Self_,
                        subdir: None,
                        storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                    })
                    .offer(OfferDecl::Storage(OfferStorageDecl {
                        source: OfferSource::Self_,
                        target: OfferTarget::static_child("d".to_string()),
                        source_name: "data".into(),
                        target_name: "data".into(),
                        availability: Availability::Required,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Capability("data".into()),
                        source_name: "unrelated.protocol".into(),
                        target_name: "fuchsia.sys2.StorageAdmin".into(),
                        target: ExposeTarget::Parent,
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
                        availability: Availability::Required,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b"].into(),
                CheckUse::StorageAdmin {
                    storage_relation: InstancedRelativeMoniker::new(vec!["d:0".into()]),
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::ErrWithNoEpitaph,
                },
            )
            .await;
    }
}
