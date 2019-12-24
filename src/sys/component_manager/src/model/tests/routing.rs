// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, FrameworkCapability},
        framework::REALM_SERVICE,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
            testing::{routing_test_helpers::*, test_helpers::*},
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::*,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc::Receiver, future::BoxFuture, join, lock::Mutex, stream::StreamExt,
        TryStreamExt,
    },
    log::*,
    std::{
        convert::{TryFrom, TryInto},
        sync::{Arc, Weak},
    },
};

///   a
///    \
///     b
///
/// b: uses framework service /svc/fuchsia.sys2.Realm
#[fuchsia_async::run_singlethreaded(test)]
async fn use_framework_service() {
    pub struct MockRealmCapabilityProvider {
        scope_moniker: AbsoluteMoniker,
        host: MockRealmCapabilityHost,
    }

    impl MockRealmCapabilityProvider {
        pub fn new(scope_moniker: AbsoluteMoniker, host: MockRealmCapabilityHost) -> Self {
            Self { scope_moniker, host }
        }
    }

    #[async_trait]
    impl CapabilityProvider for MockRealmCapabilityProvider {
        async fn open(
            self: Box<Self>,
            _flags: u32,
            _open_mode: u32,
            _relative_path: String,
            server_end: zx::Channel,
        ) -> Result<(), ModelError> {
            let stream = ServerEnd::<fsys::RealmMarker>::new(server_end)
                .into_stream()
                .expect("could not convert channel into stream");
            let scope_moniker = self.scope_moniker.clone();
            let host = self.host.clone();
            fasync::spawn(async move {
                if let Err(e) = host.serve(scope_moniker, stream).await {
                    // TODO: Set an epitaph to indicate this was an unexpected error.
                    warn!("serve_realm failed: {}", e);
                }
            });
            Ok(())
        }
    }

    impl Hook for MockRealmCapabilityHost {
        fn on(self: Arc<Self>, event: &Event) -> BoxFuture<Result<(), ModelError>> {
            Box::pin(async move {
                if let EventPayload::RouteCapability {
                    source:
                        CapabilitySource::Framework { capability, scope_moniker: Some(scope_moniker) },
                    capability_provider,
                } = &event.payload
                {
                    let mut capability_provider = capability_provider.lock().await;
                    *capability_provider = self
                        .on_route_scoped_framework_capability_async(
                            scope_moniker.clone(),
                            &capability,
                            capability_provider.take(),
                        )
                        .await?;
                }
                Ok(())
            })
        }
    }

    #[derive(Clone)]
    pub struct MockRealmCapabilityHost {
        /// List of calls to `BindChild` with component's relative moniker.
        bind_calls: Arc<Mutex<Vec<String>>>,
    }

    impl MockRealmCapabilityHost {
        pub fn new() -> Self {
            Self { bind_calls: Arc::new(Mutex::new(vec![])) }
        }

        pub fn bind_calls(&self) -> Arc<Mutex<Vec<String>>> {
            self.bind_calls.clone()
        }

        async fn serve(
            &self,
            scope_moniker: AbsoluteMoniker,
            mut stream: fsys::RealmRequestStream,
        ) -> Result<(), Error> {
            while let Some(request) = stream.try_next().await? {
                match request {
                    fsys::RealmRequest::BindChild { responder, .. } => {
                        self.bind_calls.lock().await.push(
                            scope_moniker
                                .path()
                                .last()
                                .expect("did not expect root component")
                                .name()
                                .to_string(),
                        );
                        responder.send(&mut Ok(()))?;
                    }
                    _ => {}
                }
            }
            Ok(())
        }

        pub async fn on_route_scoped_framework_capability_async<'a>(
            &'a self,
            scope_moniker: AbsoluteMoniker,
            capability: &'a FrameworkCapability,
            capability_provider: Option<Box<dyn CapabilityProvider>>,
        ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
            // If some other capability has already been installed, then there's nothing to
            // do here.
            match capability {
                FrameworkCapability::ServiceProtocol(capability_path)
                    if *capability_path == *REALM_SERVICE =>
                {
                    return Ok(Some(Box::new(MockRealmCapabilityProvider::new(
                        scope_moniker.clone(),
                        self.clone(),
                    )) as Box<dyn CapabilityProvider>));
                }
                _ => return Ok(capability_provider),
            }
        }
    }

    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    // RoutingTest installs the real RealmCapabilityHost. Installing the
    // MockRealmCapabilityHost here overrides the previously installed one.
    let realm_service_host = Arc::new(MockRealmCapabilityHost::new());
    test.model
        .root_realm
        .hooks
        .install(vec![HooksRegistration {
            events: vec![EventType::RouteCapability],
            callback: Arc::downgrade(&realm_service_host) as Weak<dyn Hook>,
        }])
        .await;
    test.check_use_realm(vec!["b:0"].into(), realm_service_host.bind_calls()).await;
}

///   a
///    \
///     b
///
/// a: offers directory /data/foo from self as /data/bar
/// a: offers service /svc/foo from self as /svc/bar
/// a: offers service /svc/file from self as /svc/device
/// b: uses directory /data/bar as /data/hippo
/// b: uses service /svc/bar as /svc/hippo
/// b: uses service /svc/device
///
/// The test related to `/svc/file` is used to verify that services that require
/// extended flags, like `OPEN_FLAG_DESCRIBE`, work correctly. This often
/// happens for fuchsia.hardware protocols that compose fuchsia.io protocols,
/// and expect that `fdio_open` should operate correctly.
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_parent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/file").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/device").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/device").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/device").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
    test.check_open_file(vec!["b:0"].into(), "/svc/device".try_into().unwrap()).await
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers directory /data/foo from self as /data/bar
/// a: offers service /svc/foo from self as /svc/bar
/// b: offers directory /data/bar from realm as /data/baz
/// b: offers service /svc/bar from realm as /svc/baz
/// c: uses /data/baz as /data/hippo
/// c: uses /svc/baz as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Realm,
                    source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// a: offers service /svc/builtin.Echo from realm
/// b: offers service /svc/builtin.Echo from realm
/// c: uses /svc/builtin.Echo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_builtin_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/builtin.Echo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/builtin.Echo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/builtin.Echo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/builtin.Echo").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/builtin.Echo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///     a
///    /
///   b
///  / \
/// d   c
///
/// d: exposes directory /data/foo from self as /data/bar
/// b: offers directory /data/bar from d as /data/foobar to c
/// c: uses /data/foobar as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_sibling_no_root() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("d".to_string()),
                    source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Child("d".to_string()),
                    source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("c")
                .add_lazy_child("d")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///   a
///  / \
/// b   c
///
/// b: exposes directory /data/foo from self as /data/bar
/// a: offers directory /data/bar from b as /data/baz to c
/// c: uses /data/baz as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_sibling_root() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Child("b".to_string()),
                    source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///     a
///    / \
///   b   c
///  /
/// d
///
/// d: exposes directory /data/foo from self as /data/bar
/// b: exposes directory /data/bar from d as /data/baz
/// a: offers directory /data/baz from b as /data/foobar to c
/// c: uses /data/foobar as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_niece() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Child("b".to_string()),
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("d".to_string()),
                    source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Child("d".to_string()),
                    source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .add_lazy_child("d")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/foobar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/foobar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///      a
///     / \
///    /   \
///   b     c
///  / \   / \
/// d   e f   g
///            \
///             h
///
/// a,d,h: hosts /svc/foo and /data/foo
/// e: uses /svc/foo as /svc/hippo from a, uses /data/foo as /data/hippo from d
/// f: uses /data/foo from d as /data/hippo, uses /svc/foo from h as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_kitchen_sink() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("d".to_string()),
                    source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target: OfferTarget::Child("e".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                    target: OfferTarget::Child("e".to_string()),
                }))
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("d".to_string()),
                    source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .add_lazy_child("d")
                .add_lazy_child("e")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Realm,
                    source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target: OfferTarget::Child("f".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Child("g".to_string()),
                    source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    target: OfferTarget::Child("f".to_string()),
                }))
                .add_lazy_child("f")
                .add_lazy_child("g")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "e",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/foo_from_a").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "f",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/foo_from_d").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "g",
            ComponentDeclBuilder::new_empty_component()
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Child("h".to_string()),
                    source_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .add_lazy_child("h")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "h",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/foo_from_h").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "e:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["b:0", "e:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["c:0", "f:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["c:0", "f:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///  component manager's namespace
///   |
///   a
///    \
///     b
///
/// a: offers directory /hippo/data/foo from realm as /foo
/// a: offers service /hippo/svc/foo from realm as /echo/echo
/// b: uses directory /foo as /data/hippo
/// b: uses service /echo/echo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_component_manager_namespace() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Realm,
                    source_path: CapabilityPath::try_from("/hippo/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/foo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Realm,
                    source_path: CapabilityPath::try_from("/hippo/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/echo/echo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/echo/echo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.install_hippo_dir();
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///   a
///    \
///     b
///
/// b: uses directory /data/hippo as /data/hippo, but it's not in its realm
/// b: uses service /svc/hippo as /svc/hippo, but it's not in its realm
#[fuchsia_async::run_singlethreaded(test)]
async fn use_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
}

///   a
///  / \
/// b   c
///
/// a: offers directory /data/hippo from b as /data/hippo, but it's not exposed by b
/// a: offers service /svc/hippo from b as /svc/hippo, but it's not exposed by b
/// c: uses directory /data/hippo as /data/hippo
/// c: uses service /svc/hippo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_offer_source_not_exposed() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    source: OfferDirectorySource::Child("b".to_string()),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    source: OfferServiceSource::Child("b".to_string()),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        ("b", component_decl_with_test_runner()),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// b: offers directory /data/hippo from its realm as /data/hippo, but it's not offered by a
/// b: offers service /svc/hippo from its realm as /svc/hippo, but it's not offfered by a
/// c: uses directory /data/hippo as /data/hippo
/// c: uses service /svc/hippo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_offer_source_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    source: OfferDirectorySource::Realm,
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    source: OfferServiceSource::Realm,
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// b: uses directory /data/hippo as /data/hippo, but it's exposed to it, not offered
/// b: uses service /svc/hippo as /svc/hippo, but it's exposed to it, not offered
/// c: exposes /data/hippo
/// c: exposes /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_expose() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    source: ExposeSource::Self_,
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    source: ExposeSource::Self_,
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
}

///   a
///  / \
/// b   c
///
/// b: exposes directory /data/foo from self as /data/bar to framework (NOT realm)
/// a: offers directory /data/bar from b as /data/baz to c, but it is not exposed via realm
/// c: uses /data/baz as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_expose_to_framework() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source: OfferServiceSource::Child("b".to_string()),
                    source_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar").unwrap(),
                    target: ExposeTarget::Framework,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/bar").unwrap(),
                    target: ExposeTarget::Framework,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/baz").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
}

///   a
///    \
///     b
///
/// a: offers directory /data/hippo to b, but a is not executable
/// a: offers service /svc/hippo to b, but a is not executable
/// b: uses directory /data/hippo as /data/hippo, but it's not in its realm
/// b: uses service /svc/hippo as /svc/hippo, but it's not in its realm
#[fuchsia_async::run_singlethreaded(test)]
async fn offer_from_non_executable() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    source: OfferDirectorySource::Self_,
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    source: OfferServiceSource::Self_,
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
}

///   a
///    \
///     b
///    / \
///  [c] [d]
/// a: offers service /svc/hippo to b
/// b: offers service /svc/hippo to collection, creates [c]
/// [c]: instance in collection uses service /svc/hippo
/// [d]: ditto, but with /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_in_collection() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    source: OfferDirectorySource::Self_,
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    source: OfferServiceSource::Self_,
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    source: OfferDirectorySource::Realm,
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: OfferTarget::Collection("coll".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    source: OfferServiceSource::Realm,
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: OfferTarget::Collection("coll".to_string()),
                }))
                .add_collection("coll", fsys::Durability::Transient)
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    // `RealmCapabilityHost` is needed to create dynamic children.
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
    test.create_dynamic_child(
        vec!["b:0"].into(),
        "coll",
        ChildDecl {
            name: "d".to_string(),
            url: "test:///d".to_string(),
            startup: fsys::StartupMode::Lazy,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:d:2"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///   a
///    \
///     b
///      \
///      [c]
/// a: offers service /svc/hippo to b
/// b: creates [c]
/// [c]: tries to use /svc/hippo, but can't because service was not offered to its collection
#[fuchsia_async::run_singlethreaded(test)]
async fn use_in_collection_not_offered() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    source: OfferDirectorySource::Self_,
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(fio2::Operations::Connect),
                }))
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    source: OfferServiceSource::Self_,
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
                .add_collection("coll", fsys::Durability::Transient)
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: fio2::Operations::Connect,
                }))
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    // `RealmCapabilityHost` is needed to create dynamic children.
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
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
}

/// Wait for a ComponentRunnerStart request, acknowledge it, and return
/// the start info.
///
/// Panics if the channel closes before we receive a request.
async fn wait_for_runner_request(
    recv: &mut Receiver<fsys::ComponentRunnerRequest>,
) -> fsys::ComponentStartInfo {
    let fsys::ComponentRunnerRequest::Start { start_info, responder, .. } =
        recv.next().await.expect("Channel closed before request was received.");
    responder.send(&mut Ok(())).expect("Failed to send response over channel.");
    start_info
}

///   a
///    \
///     b
///      \
///       c
///
/// a: declares runner "elf" as service "/svc/runner" from self.
/// a: offers runner "elf" from self to "b" as "dwarf".
/// b: offers runner "dwarf" from parent to "c" as "hobbit".
/// c: uses runner "hobbit".
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Runner(OfferRunnerDecl {
                    source: OfferRunnerSource::Self_,
                    source_name: CapabilityName("elf".to_string()),
                    target: OfferTarget::Child("b".to_string()),
                    target_name: CapabilityName("dwarf".to_string()),
                }))
                .add_lazy_child("b")
                .runner(RunnerDecl {
                    name: "elf".to_string(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Runner(OfferRunnerDecl {
                    source: OfferRunnerSource::Realm,
                    source_name: CapabilityName("dwarf".to_string()),
                    target: OfferTarget::Child("c".to_string()),
                    target_name: CapabilityName("hobbit".to_string()),
                }))
                .add_lazy_child("c")
                .build(),
        ),
        ("c", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, mut receiver) =
        create_service_directory_entry::<fsys::ComponentRunnerMarker>();
    let universe = RoutingTestBuilder::new("a", components)
        // Component "a" exposes a runner service.
        .add_outgoing_path("a", CapabilityPath::try_from("/svc/runner").unwrap(), runner_service)
        .build()
        .await;

    join!(
        // Bind "c:0". We expect to see a call to our runner service for the new component.
        async move {
            universe.bind_instance(&vec!["b:0", "c:0"].into()).await.unwrap();
        },
        // Wait for a request, and ensure it has the correct URL.
        async move {
            assert_eq!(
                wait_for_runner_request(&mut receiver).await.resolved_url,
                Some("test:///c_resolved".to_string())
            );
        }
    );
}

///   a
///  / \
/// r   b
///
/// r: declares runner "elf" as service "/svc/runner" from self.
/// r: exposes runner "elf" to "a" as "dwarf".
/// a: offers runner "dwarf" from "r" to "b" as "hobbit".
/// b: uses runner "hobbit".
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_sibling() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Runner(OfferRunnerDecl {
                    source: OfferRunnerSource::Child("r".to_string()),
                    source_name: CapabilityName("dwarf".to_string()),
                    target: OfferTarget::Child("b".to_string()),
                    target_name: CapabilityName("hobbit".to_string()),
                }))
                .add_lazy_child("b")
                .add_lazy_child("r")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "r",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Runner(ExposeRunnerDecl {
                    source: ExposeSource::Self_,
                    source_name: CapabilityName("elf".to_string()),
                    target: ExposeTarget::Realm,
                    target_name: CapabilityName("dwarf".to_string()),
                }))
                .runner(RunnerDecl {
                    name: "elf".to_string(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        ("b", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, mut receiver) =
        create_service_directory_entry::<fsys::ComponentRunnerMarker>();
    let universe = RoutingTestBuilder::new("a", components)
        // Component "r" exposes a runner service.
        .add_outgoing_path("r", CapabilityPath::try_from("/svc/runner").unwrap(), runner_service)
        .build()
        .await;

    join!(
        // Bind "b:0". We expect to see a call to our runner service for the new component.
        async move {
            universe.bind_instance(&vec!["b:0"].into()).await.unwrap();
        },
        // Wait for a request, and ensure it has the correct URL.
        async move {
            assert_eq!(
                wait_for_runner_request(&mut receiver).await.resolved_url,
                Some("test:///b_resolved".to_string())
            );
        }
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_from_self_and_child() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("c".to_string()),
                    source_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/bar/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Child("c".to_string()),
                    source_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/bar/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::Directory { path: "/data/bar/hippo".try_into().unwrap(), should_succeed: true },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::ServiceProtocol {
            path: "/svc/bar/hippo".try_into().unwrap(),
            should_succeed: true,
        },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn use_not_exposed() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .add_lazy_child("c")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                    rights: Some(fio2::Operations::Connect),
                }))
                .expose(ExposeDecl::ServiceProtocol(ExposeServiceProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    target: ExposeTarget::Realm,
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    // Capability is only exposed from "c", so it only be usable from there.
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: false },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: false },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory { path: default_directory_capability(), should_succeed: true },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::ServiceProtocol { path: default_service_capability(), should_succeed: true },
    )
    .await;
}

///   (cm)
///    |
///    a
///
/// a: uses an invalid service from the component manager.
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_use_from_component_manager() {
    let components = vec![(
        "a",
        ComponentDeclBuilder::new()
            .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                source: UseSource::Realm,
                source_path: CapabilityPath::try_from("/invalid").unwrap(),
                target_path: CapabilityPath::try_from("/svc/valid").unwrap(),
            }))
            .offer_runner_to_children(TEST_RUNNER_NAME)
            .build(),
    )];

    // Try and use the service. We expect a failure.
    let universe = RoutingTest::new("a", components).await;
    universe
        .check_use(
            vec![].into(),
            CheckUse::ServiceProtocol {
                path: CapabilityPath::try_from("/svc/valid").unwrap(),
                should_succeed: false,
            },
        )
        .await;
}

///   (cm)
///    |
///    a
///    |
///    b
///
/// a: offers an invalid service from the component manager to "b".
/// b: attempts to use the service
#[fuchsia_async::run_singlethreaded(test)]
async fn invalid_offer_from_component_manager() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::ServiceProtocol(OfferServiceProtocolDecl {
                    source_path: CapabilityPath::try_from("/invalid").unwrap(),
                    source: OfferServiceSource::Realm,
                    target_path: CapabilityPath::try_from("/svc/valid").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                }))
                .add_lazy_child("b")
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::ServiceProtocol(UseServiceProtocolDecl {
                    source: UseSource::Realm,
                    source_path: CapabilityPath::try_from("/svc/valid").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/valid").unwrap(),
                }))
                .offer_runner_to_children(TEST_RUNNER_NAME)
                .build(),
        ),
    ];

    // Try and use the service. We expect a failure.
    let universe = RoutingTest::new("a", components).await;
    universe
        .check_use(
            vec!["b:0"].into(),
            CheckUse::ServiceProtocol {
                path: CapabilityPath::try_from("/svc/valid").unwrap(),
                should_succeed: false,
            },
        )
        .await;
}
