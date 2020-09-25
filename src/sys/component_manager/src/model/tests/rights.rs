// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        channel,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            rights,
            testing::{routing_test_helpers::*, test_helpers::*},
        },
    },
    async_trait::async_trait,
    cm_rust::*,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    fuchsia_zircon as zx,
    std::{
        convert::TryFrom,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

#[fuchsia_async::run_singlethreaded(test)]
async fn offer_increasing_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("foo_data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn offer_incompatible_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::WRITE_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("foo_data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_increasing_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("foo_data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_incompatible_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("foo_data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn capability_increasing_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("foo_data")
                        .rights(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS)
                        .build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn capability_incompatible_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(
                    DirectoryDeclBuilder::new("foo_data").rights(*rights::WRITE_RIGHTS).build(),
                )
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS | *rights::WRITE_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
}

struct MockFrameworkDirectoryProvider {
    test_dir_proxy: DirectoryProxy,
}
struct MockFrameworkDirectoryHost {
    test_dir_proxy: DirectoryProxy,
}

#[async_trait]
impl CapabilityProvider for MockFrameworkDirectoryProvider {
    async fn open(
        self: Box<Self>,
        flags: u32,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError> {
        let relative_path = relative_path.to_str().unwrap();
        let server_end = channel::take_channel(server_end);
        let server_end = ServerEnd::<fio::NodeMarker>::new(server_end);
        self.test_dir_proxy
            .open(flags, open_mode, relative_path, server_end)
            .expect("failed to open test dir");
        Ok(())
    }
}

#[async_trait]
impl Hook for MockFrameworkDirectoryHost {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        if let Ok(EventPayload::CapabilityRouted {
            source:
                CapabilitySource::Framework {
                    capability: InternalCapability::Directory(name_or_path),
                    ..
                },
            capability_provider,
        }) = &event.result
        {
            let mut capability_provider = capability_provider.lock().await;
            match name_or_path {
                CapabilityNameOrPath::Path(_) => {}
                CapabilityNameOrPath::Name(source_name) => {
                    if source_name.str() == "foo_data" {
                        let test_dir_proxy = io_util::clone_directory(
                            &self.test_dir_proxy,
                            fio::CLONE_FLAG_SAME_RIGHTS,
                        )
                        .expect("failed to clone test dir");
                        *capability_provider =
                            Some(Box::new(MockFrameworkDirectoryProvider { test_dir_proxy }));
                    }
                }
            };
        }
        Ok(())
    }
}

#[fuchsia_async::run_singlethreaded(test)]
async fn framework_directory_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Framework,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: None,
                    subdir: Some("foo".into()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    let test_dir_proxy =
        io_util::clone_directory(&test.test_dir_proxy, fio::CLONE_FLAG_SAME_RIGHTS)
            .expect("failed to clone test dir");
    let directory_host = Arc::new(MockFrameworkDirectoryHost { test_dir_proxy });
    test.model
        .root_realm
        .hooks
        .install(vec![HooksRegistration::new(
            "MockFrameworkDirectoryHost",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(&directory_host) as Weak<dyn Hook>,
        )])
        .await;
    test.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn framework_directory_incompatible_rights() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Framework,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: None,
                    subdir: Some("foo".into()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS | *rights::WRITE_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    let test_dir_proxy =
        io_util::clone_directory(&test.test_dir_proxy, fio::CLONE_FLAG_SAME_RIGHTS)
            .expect("failed to clone test dir");
    let directory_host = Arc::new(MockFrameworkDirectoryHost { test_dir_proxy });
    test.model
        .root_realm
        .hooks
        .install(vec![HooksRegistration::new(
            "MockFrameworkDirectoryHost",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(&directory_host) as Weak<dyn Hook>,
        )])
        .await;
    test.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
}
