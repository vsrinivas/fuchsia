// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        CheckUse, ComponentEventRoute, ExpectedResult, RoutingTestModel, RoutingTestModelBuilder,
    },
    cm_moniker::InstancedRelativeMoniker,
    cm_rust::*,
    cm_rust_testing::{ComponentDeclBuilder, DirectoryDeclBuilder, ProtocolDeclBuilder},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_io as fio,
    fuchsia_zircon_status as zx_status,
    moniker::RelativeMonikerBase,
    routing::rights::{READ_RIGHTS, WRITE_RIGHTS},
    std::{
        convert::TryInto,
        marker::PhantomData,
        path::{Path, PathBuf},
    },
};

pub struct CommonAvailabilityTest<T: RoutingTestModelBuilder> {
    builder: PhantomData<T>,
}

impl<T: RoutingTestModelBuilder> CommonAvailabilityTest<T> {
    pub fn new() -> Self {
        Self { builder: PhantomData }
    }

    pub async fn test_offer_availability_successful_routes(&self) {
        #[derive(Debug)]
        struct TestCase {
            offer_availability: Availability,
            use_availability: Availability,
        }
        for test_case in &[
            TestCase {
                offer_availability: Availability::Required,
                use_availability: Availability::Required,
            },
            TestCase {
                offer_availability: Availability::Optional,
                use_availability: Availability::Optional,
            },
            TestCase {
                offer_availability: Availability::Required,
                use_availability: Availability::Optional,
            },
            TestCase {
                offer_availability: Availability::SameAsTarget,
                use_availability: Availability::Required,
            },
            TestCase {
                offer_availability: Availability::SameAsTarget,
                use_availability: Availability::Optional,
            },
            TestCase {
                offer_availability: Availability::Required,
                use_availability: Availability::Transitional,
            },
            TestCase {
                offer_availability: Availability::Optional,
                use_availability: Availability::Transitional,
            },
            TestCase {
                offer_availability: Availability::Transitional,
                use_availability: Availability::Transitional,
            },
            TestCase {
                offer_availability: Availability::SameAsTarget,
                use_availability: Availability::Transitional,
            },
        ] {
            let components = vec![
                (
                    "a",
                    ComponentDeclBuilder::new()
                        .offer(OfferDecl::Service(OfferServiceDecl {
                            source: OfferSource::static_child("b".to_string()),
                            source_name: "fuchsia.examples.EchoService".into(),
                            target_name: "fuchsia.examples.EchoService".into(),
                            target: OfferTarget::static_child("c".to_string()),
                            source_instance_filter: None,
                            renamed_instances: None,
                            availability: test_case.offer_availability.clone(),
                        }))
                        .offer(OfferDecl::Protocol(OfferProtocolDecl {
                            source: OfferSource::static_child("b".to_string()),
                            source_name: "fuchsia.examples.Echo".into(),
                            target_name: "fuchsia.examples.Echo".into(),
                            target: OfferTarget::static_child("c".to_string()),
                            dependency_type: DependencyType::Strong,
                            availability: test_case.offer_availability.clone(),
                        }))
                        .offer(OfferDecl::Directory(OfferDirectoryDecl {
                            source: OfferSource::static_child("b".to_string()),
                            source_name: "dir".try_into().unwrap(),
                            target: OfferTarget::static_child("c".to_string()),
                            target_name: "dir".try_into().unwrap(),
                            rights: Some(*READ_RIGHTS),
                            subdir: None,
                            dependency_type: DependencyType::Strong,
                            availability: test_case.offer_availability.clone(),
                        }))
                        .directory(
                            DirectoryDeclBuilder::new("data")
                                .path("/data")
                                .rights(*READ_RIGHTS | *WRITE_RIGHTS)
                                .build(),
                        )
                        .storage(StorageDecl {
                            name: "cache".into(),
                            backing_dir: "data".try_into().unwrap(),
                            source: StorageDirectorySource::Self_,
                            subdir: Some(PathBuf::from("cache")),
                            storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                        })
                        .offer(OfferDecl::Storage(OfferStorageDecl {
                            source: OfferSource::Self_,
                            target: OfferTarget::static_child("c".to_string()),
                            source_name: "cache".into(),
                            target_name: "cache".into(),
                            availability: test_case.offer_availability.clone(),
                        }))
                        .offer(OfferDecl::EventStream(OfferEventStreamDecl {
                            source: OfferSource::Parent,
                            source_name: "started".into(),
                            scope: None,
                            filter: None,
                            target: OfferTarget::Child(ChildRef {
                                name: "c".to_string(),
                                collection: None,
                            }),
                            target_name: CapabilityName::from("started"),
                            availability: test_case.offer_availability.clone(),
                        }))
                        .add_lazy_child("b")
                        .add_lazy_child("c")
                        .build(),
                ),
                (
                    "b",
                    ComponentDeclBuilder::new()
                        .service(ServiceDecl {
                            name: "fuchsia.examples.EchoService".into(),
                            source_path: Some(
                                "/svc/fuchsia.examples.EchoService".try_into().unwrap(),
                            ),
                        })
                        .expose(ExposeDecl::Service(ExposeServiceDecl {
                            source: ExposeSource::Self_,
                            source_name: "fuchsia.examples.EchoService".into(),
                            target_name: "fuchsia.examples.EchoService".into(),
                            target: ExposeTarget::Parent,
                        }))
                        .protocol(ProtocolDeclBuilder::new("fuchsia.examples.Echo").build())
                        .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                            source: ExposeSource::Self_,
                            source_name: "fuchsia.examples.Echo".into(),
                            target_name: "fuchsia.examples.Echo".into(),
                            target: ExposeTarget::Parent,
                        }))
                        .directory(
                            DirectoryDeclBuilder::new("dir")
                                .path("/data/dir")
                                .rights(*READ_RIGHTS)
                                .build(),
                        )
                        .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Self_,
                            source_name: "dir".into(),
                            target_name: "dir".into(),
                            target: ExposeTarget::Parent,
                            rights: None,
                            subdir: None,
                        }))
                        .build(),
                ),
                (
                    "c",
                    ComponentDeclBuilder::new()
                        .use_(UseDecl::Service(UseServiceDecl {
                            source: UseSource::Parent,
                            source_name: "fuchsia.examples.EchoService".into(),
                            target_path: "/svc/fuchsia.examples.EchoService".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                            availability: test_case.use_availability.clone(),
                        }))
                        .use_(UseDecl::Protocol(UseProtocolDecl {
                            source: UseSource::Parent,
                            source_name: "fuchsia.examples.Echo".into(),
                            target_path: "/svc/fuchsia.examples.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                            availability: test_case.use_availability.clone(),
                        }))
                        .use_(UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Parent,
                            source_name: "dir".try_into().unwrap(),
                            target_path: "/dir".try_into().unwrap(),
                            rights: *READ_RIGHTS,
                            subdir: None,
                            dependency_type: DependencyType::Strong,
                            availability: test_case.use_availability.clone(),
                        }))
                        .use_(UseDecl::Storage(UseStorageDecl {
                            source_name: "cache".into(),
                            target_path: "/storage".try_into().unwrap(),
                            availability: test_case.use_availability.clone(),
                        }))
                        .use_(UseDecl::EventStream(UseEventStreamDecl {
                            source: UseSource::Parent,
                            source_name: "started".into(),
                            target_path: "/event/stream".try_into().unwrap(),
                            scope: None,
                            filter: None,
                            availability: test_case.use_availability.clone(),
                        }))
                        .build(),
                ),
            ];
            let mut builder = T::new("a", components);
            builder.set_builtin_capabilities(vec![CapabilityDecl::EventStream(EventStreamDecl {
                name: "started".into(),
            })]);
            let model = builder.build().await;
            model
                .create_static_file(Path::new("dir/hippo"), "hello")
                .await
                .expect("failed to create file");
            for check_use in vec![
                CheckUse::Protocol {
                    path: "/svc/fuchsia.examples.Echo".try_into().unwrap(),
                    expected_res: ExpectedResult::Ok,
                },
                CheckUse::Directory {
                    path: "/dir".try_into().unwrap(),
                    file: PathBuf::from("hippo"),
                    expected_res: ExpectedResult::Ok,
                },
                CheckUse::Storage {
                    path: "/storage".try_into().unwrap(),
                    storage_relation: Some(InstancedRelativeMoniker::new(vec!["c:0".into()])),
                    from_cm_namespace: false,
                    storage_subdir: Some("cache".to_string()),
                    expected_res: ExpectedResult::Ok,
                },
                CheckUse::EventStream {
                    expected_res: ExpectedResult::Ok,
                    path: "/event/stream".try_into().unwrap(),
                    scope: vec![ComponentEventRoute { component: "/".to_string(), scope: None }],
                    name: "started".into(),
                },
            ] {
                model.check_use(vec!["c"].into(), check_use).await;
            }
        }
    }

    pub async fn test_offer_availability_invalid_routes(&self) {
        struct TestCase {
            source: OfferSource,
            storage_source: Option<OfferSource>,
            offer_availability: Availability,
            use_availability: Availability,
        }
        for test_case in &[
            TestCase {
                source: OfferSource::static_child("b".to_string()),
                storage_source: Some(OfferSource::Self_),
                offer_availability: Availability::Optional,
                use_availability: Availability::Required,
            },
            TestCase {
                source: OfferSource::Void,
                storage_source: None,
                offer_availability: Availability::Optional,
                use_availability: Availability::Required,
            },
            TestCase {
                source: OfferSource::Void,
                storage_source: None,
                offer_availability: Availability::Optional,
                use_availability: Availability::Optional,
            },
            TestCase {
                source: OfferSource::Void,
                storage_source: None,
                offer_availability: Availability::Transitional,
                use_availability: Availability::Optional,
            },
            TestCase {
                source: OfferSource::Void,
                storage_source: None,
                offer_availability: Availability::Transitional,
                use_availability: Availability::Required,
            },
        ] {
            let components = vec![
                (
                    "a",
                    ComponentDeclBuilder::new()
                        .offer(OfferDecl::Protocol(OfferProtocolDecl {
                            source: test_case.source.clone(),
                            source_name: "fuchsia.examples.Echo".into(),
                            target_name: "fuchsia.examples.Echo".into(),
                            target: OfferTarget::static_child("c".to_string()),
                            dependency_type: DependencyType::Strong,
                            availability: test_case.offer_availability.clone(),
                        }))
                        .offer(OfferDecl::Directory(OfferDirectoryDecl {
                            source: test_case.source.clone(),
                            source_name: "dir".try_into().unwrap(),
                            target: OfferTarget::static_child("c".to_string()),
                            target_name: "dir".try_into().unwrap(),
                            rights: Some(fio::Operations::CONNECT),
                            subdir: None,
                            dependency_type: DependencyType::Strong,
                            availability: test_case.offer_availability.clone(),
                        }))
                        .offer(OfferDecl::Storage(OfferStorageDecl {
                            source: test_case
                                .storage_source
                                .as_ref()
                                .map(Clone::clone)
                                .unwrap_or(test_case.source.clone()),
                            source_name: "data".into(),
                            target_name: "data".into(),
                            target: OfferTarget::static_child("c".to_string()),
                            availability: test_case.offer_availability.clone(),
                        }))
                        .storage(StorageDecl {
                            name: "data".into(),
                            source: StorageDirectorySource::Child("b".to_string()),
                            backing_dir: "dir".into(),
                            subdir: None,
                            storage_id: fdecl::StorageId::StaticInstanceIdOrMoniker,
                        })
                        .add_lazy_child("b")
                        .add_lazy_child("c")
                        .build(),
                ),
                (
                    "b",
                    ComponentDeclBuilder::new()
                        .protocol(ProtocolDeclBuilder::new("fuchsia.examples.Echo").build())
                        .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                            source: ExposeSource::Self_,
                            source_name: "fuchsia.examples.Echo".into(),
                            target_name: "fuchsia.examples.Echo".into(),
                            target: ExposeTarget::Parent,
                        }))
                        .directory(
                            DirectoryDeclBuilder::new("dir")
                                .path("/dir")
                                .rights(fio::Operations::CONNECT)
                                .build(),
                        )
                        .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                            source: ExposeSource::Self_,
                            source_name: "dir".into(),
                            target_name: "dir".into(),
                            target: ExposeTarget::Parent,
                            rights: None,
                            subdir: None,
                        }))
                        .build(),
                ),
                (
                    "c",
                    ComponentDeclBuilder::new()
                        .use_(UseDecl::Protocol(UseProtocolDecl {
                            source: UseSource::Parent,
                            source_name: "fuchsia.examples.Echo".into(),
                            target_path: "/svc/fuchsia.examples.Echo".try_into().unwrap(),
                            dependency_type: DependencyType::Strong,
                            availability: test_case.use_availability.clone(),
                        }))
                        .use_(UseDecl::Directory(UseDirectoryDecl {
                            source: UseSource::Parent,
                            source_name: "dir".try_into().unwrap(),
                            target_path: "/dir".try_into().unwrap(),
                            rights: fio::Operations::CONNECT,
                            subdir: None,
                            dependency_type: DependencyType::Strong,
                            availability: test_case.use_availability.clone(),
                        }))
                        .use_(UseDecl::Storage(UseStorageDecl {
                            source_name: "data".try_into().unwrap(),
                            target_path: "/data".try_into().unwrap(),
                            availability: test_case.use_availability.clone(),
                        }))
                        .build(),
                ),
            ];
            let model = T::new("a", components).build().await;
            for check_use in vec![
                CheckUse::Protocol {
                    path: "/svc/fuchsia.examples.Echo".try_into().unwrap(),
                    expected_res: ExpectedResult::Err(zx_status::Status::UNAVAILABLE),
                },
                CheckUse::Directory {
                    path: "/dir".try_into().unwrap(),
                    file: PathBuf::from("hippo"),
                    expected_res: ExpectedResult::Err(zx_status::Status::UNAVAILABLE),
                },
                CheckUse::Storage {
                    path: "/data".try_into().unwrap(),
                    storage_relation: None,
                    from_cm_namespace: false,
                    storage_subdir: None,
                    expected_res: ExpectedResult::Err(zx_status::Status::UNAVAILABLE),
                },
            ] {
                model.check_use(vec!["c"].into(), check_use).await;
            }
        }
    }
}
