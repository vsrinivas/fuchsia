// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::{CapabilityProvider, CapabilitySource, InternalCapability},
        channel,
        framework::REALM_SERVICE,
        model::{
            error::ModelError,
            hooks::{Event, EventPayload, EventType, Hook, HooksRegistration},
            moniker::AbsoluteMoniker,
            resolver::ResolverError,
            rights,
            routing::{self, RoutingError},
            testing::{
                routing_test_helpers::capability_util::connect_to_svc_in_namespace,
                routing_test_helpers::*, test_helpers::*,
            },
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::*,
    fidl::endpoints::ServerEnd,
    fidl_fidl_examples_echo::{self as echo},
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::{MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_zircon as zx,
    futures::{join, lock::Mutex, StreamExt, TryStreamExt},
    log::*,
    maplit::hashmap,
    matches::assert_matches,
    std::{
        convert::{TryFrom, TryInto},
        path::{Path, PathBuf},
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
            _relative_path: PathBuf,
            server_end: &mut zx::Channel,
        ) -> Result<(), ModelError> {
            let server_end = channel::take_channel(server_end);
            let stream = ServerEnd::<fsys::RealmMarker>::new(server_end)
                .into_stream()
                .expect("could not convert channel into stream");
            let scope_moniker = self.scope_moniker.clone();
            let host = self.host.clone();
            fasync::Task::spawn(async move {
                if let Err(e) = host.serve(scope_moniker, stream).await {
                    // TODO: Set an epitaph to indicate this was an unexpected error.
                    warn!("serve_realm failed: {}", e);
                }
            })
            .detach();
            Ok(())
        }
    }

    #[async_trait]
    impl Hook for MockRealmCapabilityHost {
        async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
            if let Ok(EventPayload::CapabilityRouted {
                source: CapabilitySource::Framework { capability, scope_moniker },
                capability_provider,
            }) = &event.result
            {
                let mut capability_provider = capability_provider.lock().await;
                *capability_provider = self
                    .on_scoped_framework_capability_routed_async(
                        scope_moniker.clone(),
                        &capability,
                        capability_provider.take(),
                    )
                    .await?;
            }
            Ok(())
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

        pub async fn on_scoped_framework_capability_routed_async<'a>(
            &'a self,
            scope_moniker: AbsoluteMoniker,
            capability: &'a InternalCapability,
            capability_provider: Option<Box<dyn CapabilityProvider>>,
        ) -> Result<Option<Box<dyn CapabilityProvider>>, ModelError> {
            // If some other capability has already been installed, then there's nothing to
            // do here.
            if capability.matches_protocol(&REALM_SERVICE) {
                Ok(Some(Box::new(MockRealmCapabilityProvider::new(
                    scope_moniker.clone(),
                    self.clone(),
                )) as Box<dyn CapabilityProvider>))
            } else {
                Ok(capability_provider)
            }
        }
    }

    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
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
        .install(vec![HooksRegistration::new(
            "MockRealmCapabilityHost",
            vec![EventType::CapabilityRouted],
            Arc::downgrade(&realm_service_host) as Weak<dyn Hook>,
        )])
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
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .protocol(ProtocolDeclBuilder::new("file").path("/svc/file").build())
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("file").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("device").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("device").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/device").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
    )
    .await;
    test.check_open_file(vec!["b:0"].into(), "/svc/device".try_into().unwrap()).await;
}

///   a
///    \
///     b
///
/// a: offers service /svc/foo from self as /svc/bar
/// b: uses service /svc/bar as /svc/hippo
///
/// This test verifies that the parent, if subscribed to the CapabilityRequested event will receive
/// if when the child connects to /svc/hippo.
#[fuchsia_async::run_singlethreaded(test)]
async fn capability_requested_event_at_parent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: "fuchsia.sys2.EventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "capability_requested".into(),
                    target_name: "capability_requested".into(),
                    filter: Some(hashmap!{"path".to_string() => DictionaryValue::Str("/svc/foo".to_string())}),
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;

    let namespace_root = test.bind_and_get_namespace(AbsoluteMoniker::root()).await;
    let mut event_stream = capability_util::subscribe_to_events(
        &namespace_root,
        &CapabilityPath::try_from("/svc/fuchsia.sys2.EventSource").unwrap(),
        vec!["capability_requested".into()],
    )
    .await
    .unwrap();

    let namespace_b = test.bind_and_get_namespace(vec!["b:0"].into()).await;
    let _echo_proxy = connect_to_svc_in_namespace::<echo::EchoMarker>(
        &namespace_b,
        &"/svc/hippo".try_into().unwrap(),
    )
    .await;

    let event = match event_stream.next().await {
        Some(Ok(fsys::EventStreamRequest::OnEvent { event, .. })) => event,
        _ => panic!("Event not found"),
    };

    // 'b' is the target and 'a' is receiving the event so the relative moniker
    // is './b:0'.
    assert_matches!(&event,
        fsys::Event {
            descriptor: Some(fsys::ComponentDescriptor {
            moniker: Some(moniker), .. }), ..
        } if *moniker == "./b:0".to_string() );

    assert_matches!(&event,
        fsys::Event {
            descriptor: Some(fsys::ComponentDescriptor {
            component_url: Some(component_url), .. }), ..
        } if *component_url == "test:///b".to_string() );

    assert_matches!(&event,
        fsys::Event {
            event_result: Some(
                fsys::EventResult::Payload(
                        fsys::EventPayload::CapabilityRequested(
                            fsys::CapabilityRequestedPayload { path: Some(path), .. }))), ..}

    if *path == "/svc/foo".to_string()
    );
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
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Parent,
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("c")
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
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0", "c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
        .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
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
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/svc/builtin.Echo").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/builtin.Echo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/svc/builtin.Echo").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/builtin.Echo").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/svc/builtin.Echo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
    )
    .await;
}

/// Tests legacy path-based capability routing
#[fuchsia_async::run_singlethreaded(test)]
async fn route_by_path_legacy() {
    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/data/bar").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/data/foobar").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("/svc/bar").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/foobar").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("c")
                .add_lazy_child("d")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/data/foobar").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/svc/foobar").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("/data/foo").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/data/bar").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("/svc/foo").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/svc/bar").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0", "c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
        .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
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
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foobar_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foobar_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("c")
                .add_lazy_child("d")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foobar_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foobar_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0", "c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
        .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
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
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target: ExposeTarget::Parent,
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
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
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
                    source_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foobar_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foobar_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .add_lazy_child("d")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foobar_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foobar_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
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
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_a_svc").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
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
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target: OfferTarget::Child("e".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_from_a_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_a_svc").unwrap(),
                    target: OfferTarget::Child("e".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("d".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .add_lazy_child("d")
                .add_lazy_child("e")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target: OfferTarget::Child("f".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("g".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_from_h_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_h_svc").unwrap(),
                    target: OfferTarget::Child("f".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("f")
                .add_lazy_child("g")
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
        (
            "e",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_from_a_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
        (
            "f",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_from_d_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_from_h_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
        (
            "g",
            ComponentDeclBuilder::new_empty_component()
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Child("h".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_from_h_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_h_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .add_lazy_child("h")
                .build(),
        ),
        (
            "h",
            ComponentDeclBuilder::new()
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_from_h_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0", "e:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
        .await;
    test.check_use(
        vec!["b:0", "e:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
    )
    .await;
    test.check_use(vec!["c:0", "f:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
        .await;
    test.check_use(
        vec!["c:0", "f:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
    )
    .await;
}

///  component manager's namespace
///   |
///   a
///
/// a: uses directory /use_from_cm_namespace/data/foo as /data/hippo
/// a: uses service /use_from_cm_namespace/svc/foo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_component_manager_namespace() {
    let components = vec![(
        "a",
        ComponentDeclBuilder::new()
            .use_(UseDecl::Directory(UseDirectoryDecl {
                source: UseSource::Parent,
                source_path: CapabilityNameOrPath::try_from("/use_from_cm_namespace/data/foo")
                    .unwrap(),
                target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                rights: fio2::Operations::Connect,
                subdir: None,
            }))
            .use_(UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Parent,
                source_path: CapabilityNameOrPath::try_from("/use_from_cm_namespace/svc/foo")
                    .unwrap(),
                target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            }))
            .build(),
    )];
    let test = RoutingTest::new("a", components).await;
    test.install_hippo_dir("/use_from_cm_namespace");
    test.check_use(vec![].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
    test.check_use(
        vec![].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
    )
    .await;
}

///  component manager's namespace
///   |
///   a
///    \
///     b
///
/// a: offers directory /offer_from_cm_namespace/data/foo from realm as /foo
/// a: offers service /offer_from_cm_namespace/svc/foo from realm as /echo/echo
/// b: uses directory /foo as /data/hippo
/// b: uses service /echo/echo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn offer_from_component_manager_namespace() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Parent,
                    source_path: CapabilityNameOrPath::try_from(
                        "/offer_from_cm_namespace/data/foo",
                    )
                    .unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/foo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/offer_from_cm_namespace/svc/foo")
                        .unwrap(),
                    target_path: CapabilityNameOrPath::try_from("/echo/echo").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                    source_path: CapabilityNameOrPath::try_from("/foo").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/echo/echo").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.install_hippo_dir("/offer_from_cm_namespace");
    test.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
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
        ("a", ComponentDeclBuilder::new_empty_component().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Protocol {
            path: default_service_capability(),
            expected_res: ExpectedResult::Err,
        },
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
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    source: OfferDirectorySource::Child("b".to_string()),
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    source: OfferServiceSource::Child("b".to_string()),
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        ("b", component_decl_with_test_runner()),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Protocol {
            path: default_service_capability(),
            expected_res: ExpectedResult::Err,
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
/// b: offers directory /data/hippo from its realm as /data/hippo, but it's not offered by a
/// b: offers service /svc/hippo from its realm as /svc/hippo, but it's not offfered by a
/// c: uses directory /data/hippo as /data/hippo
/// c: uses service /svc/hippo as /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_offer_source_not_offered() {
    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new_empty_component()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    source: OfferDirectorySource::Parent,
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    source: OfferServiceSource::Parent,
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0", "c:0"].into(), CheckUse::default_directory(ExpectedResult::Err))
        .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Protocol {
            path: default_service_capability(),
            expected_res: ExpectedResult::Err,
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
/// b: uses directory /data/hippo as /data/hippo, but it's exposed to it, not offered
/// b: uses service /svc/hippo as /svc/hippo, but it's exposed to it, not offered
/// c: exposes /data/hippo
/// c: exposes /svc/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_from_expose() {
    let components = vec![
        ("a", ComponentDeclBuilder::new_empty_component().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("hippo_data").build())
                .protocol(ProtocolDeclBuilder::new("hippo_svc").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    source: ExposeSource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    source: ExposeSource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Protocol {
            path: default_service_capability(),
            expected_res: ExpectedResult::Err,
        },
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
                    source_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_data").unwrap(),
                    target: ExposeTarget::Framework,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("bar_svc").unwrap(),
                    target: ExposeTarget::Framework,
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
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("baz_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Protocol {
            path: default_service_capability(),
            expected_res: ExpectedResult::Err,
        },
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
                .directory(DirectoryDeclBuilder::new("hippo_data").build())
                .protocol(ProtocolDeclBuilder::new("hippo_svc").build())
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    source: OfferDirectorySource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    source: OfferServiceSource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Err)).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Protocol {
            path: default_service_capability(),
            expected_res: ExpectedResult::Err,
        },
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
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    source: OfferDirectorySource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    source: OfferServiceSource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    source: OfferDirectorySource::Parent,
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: OfferTarget::Collection("coll".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    source: OfferServiceSource::Parent,
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: OfferTarget::Collection("coll".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_transient_collection("coll")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
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
            environment: None,
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
            environment: None,
        },
    )
    .await;
    test.check_use(vec!["b:0", "coll:c:1"].into(), CheckUse::default_directory(ExpectedResult::Ok))
        .await;
    test.check_use(
        vec!["b:0", "coll:d:2"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
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
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    source: OfferDirectorySource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    source: OfferServiceSource::Self_,
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
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
                    source_path: CapabilityNameOrPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
                .add_transient_collection("coll")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: None,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
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
            environment: None,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::default_directory(ExpectedResult::Err),
    )
    .await;
    test.check_use(
        vec!["b:0", "coll:c:1"].into(),
        CheckUse::Protocol {
            path: default_service_capability(),
            expected_res: ExpectedResult::Err,
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
/// a: offers directory /data/foo from self with subdir 's1/s2'
/// b: offers directory /data/foo from realm with subdir 's3'
/// c: uses /data/foo as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_directory_with_subdir_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: Some(PathBuf::from("s1/s2")),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: Some(PathBuf::from("s3")),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *rights::READ_RIGHTS,
                    subdir: Some(PathBuf::from("s4")),
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_static_file(Path::new("foo/s1/s2/s3/s4/inner"), "hippo")
        .await
        .expect("failed to create file");
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory {
            path: default_directory_capability(),
            file: PathBuf::from("inner"),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
}

///   a
///  / \
/// b   c
///
///
/// b: exposes directory /data/foo from self with subdir 's1/s2'
/// a: offers directory /data/foo from `b` to `c` with subdir 's3'
/// c: uses /data/foo as /data/hippo
#[fuchsia_async::run_singlethreaded(test)]
async fn use_directory_with_subdir_from_sibling() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Directory(OfferDirectoryDecl {
                    source: OfferDirectorySource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: Some(PathBuf::from("s3")),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .add_lazy_child("c")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: ExposeTarget::Parent,
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: Some(PathBuf::from("s1/s2")),
                }))
                .build(),
        ),
        (
            "c",
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
    test.create_static_file(Path::new("foo/s1/s2/s3/inner"), "hippo")
        .await
        .expect("failed to create file");
    test.check_use(
        vec!["c:0"].into(),
        CheckUse::Directory {
            path: default_directory_capability(),
            file: PathBuf::from("inner"),
            expected_res: ExpectedResult::Ok,
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
/// c: exposes /data/foo from self
/// b: exposes /data/foo from `c` with subdir `s1/s2`
/// a: exposes /data/foo from `b` with subdir `s3` as /data/hippo
/// use /data/hippo from a's exposed dir
#[fuchsia_async::run_singlethreaded(test)]
async fn expose_directory_with_subdir() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("b".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: None,
                    subdir: Some(PathBuf::from("s3")),
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("c".to_string()),
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: None,
                    subdir: Some(PathBuf::from("s1/s2")),
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_static_file(Path::new("foo/s1/s2/s3/inner"), "hippo")
        .await
        .expect("failed to create file");
    test.check_use_exposed_dir(
        vec![].into(),
        CheckUse::Directory {
            path: "/hippo_data".try_into().unwrap(),
            file: PathBuf::from("inner"),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
}

///  a
///   \
///    b
///
/// a: declares runner "elf" with service "/svc/runner" from "self".
/// a: registers runner "elf" from self in environment as "hobbit".
/// b: uses runner "hobbit".
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_parent_environment() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(RunnerRegistration {
                            source_name: "elf".into(),
                            source: RegistrationSource::Self_,
                            target_name: "hobbit".into(),
                        })
                        .build(),
                )
                .runner(RunnerDecl {
                    name: "elf".into(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .build(),
        ),
        ("b", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, mut receiver) =
        create_service_directory_entry::<fcrunner::ComponentRunnerMarker>();
    let universe = RoutingTestBuilder::new("a", components)
        // Component "b" exposes a runner service.
        .add_outgoing_path("a", CapabilityPath::try_from("/svc/runner").unwrap(), runner_service)
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

///  a
///   \
///   [b]
///
/// a: declares runner "elf" with service "/svc/runner" from "self".
/// a: registers runner "elf" from self in environment as "hobbit".
/// b: instance in collection uses runner "hobbit".
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_environment_in_collection() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_collection(
                    CollectionDeclBuilder::new_transient_collection("coll")
                        .environment("env")
                        .build(),
                )
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(RunnerRegistration {
                            source_name: "elf".into(),
                            source: RegistrationSource::Self_,
                            target_name: "hobbit".into(),
                        })
                        .build(),
                )
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
                .runner(RunnerDecl {
                    name: "elf".into(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .build(),
        ),
        ("b", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, mut receiver) =
        create_service_directory_entry::<fcrunner::ComponentRunnerMarker>();
    let universe = RoutingTestBuilder::new("a", components)
        // Component "a" exposes a runner service.
        .add_outgoing_path("a", CapabilityPath::try_from("/svc/runner").unwrap(), runner_service)
        .build()
        .await;
    universe
        .create_dynamic_child(
            AbsoluteMoniker::root(),
            "coll",
            ChildDecl {
                name: "b".to_string(),
                url: "test:///b".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
            },
        )
        .await;

    join!(
        // Bind "coll:b:1". We expect to see a call to our runner service for the new component.
        async move {
            universe.bind_instance(&vec!["coll:b:1"].into()).await.unwrap();
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

///   a
///    \
///     b
///      \
///       c
///
/// a: declares runner "elf" as service "/svc/runner" from self.
/// a: offers runner "elf" from self to "b" as "dwarf".
/// b: registers runner "dwarf" from realm in environment as "hobbit".
/// c: uses runner "hobbit".
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_grandparent_environment() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .offer(OfferDecl::Runner(OfferRunnerDecl {
                    source: OfferRunnerSource::Self_,
                    source_name: CapabilityName("elf".to_string()),
                    target: OfferTarget::Child("b".to_string()),
                    target_name: CapabilityName("dwarf".to_string()),
                }))
                .runner(RunnerDecl {
                    name: "elf".into(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env").build())
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(RunnerRegistration {
                            source_name: "dwarf".into(),
                            source: RegistrationSource::Parent,
                            target_name: "hobbit".into(),
                        })
                        .build(),
                )
                .build(),
        ),
        ("c", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, mut receiver) =
        create_service_directory_entry::<fcrunner::ComponentRunnerMarker>();
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
/// b   c
///
/// a: registers runner "dwarf" from "b" in environment as "hobbit".
/// b: exposes runner "elf" as service "/svc/runner" from self as "dwarf".
/// c: uses runner "hobbit".
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_sibling_environment() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_lazy_child("b")
                .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env").build())
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(RunnerRegistration {
                            source_name: "dwarf".into(),
                            source: RegistrationSource::Child("b".into()),
                            target_name: "hobbit".into(),
                        })
                        .build(),
                )
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Runner(ExposeRunnerDecl {
                    source: ExposeSource::Self_,
                    source_name: CapabilityName("elf".to_string()),
                    target: ExposeTarget::Parent,
                    target_name: CapabilityName("dwarf".to_string()),
                }))
                .runner(RunnerDecl {
                    name: "elf".into(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .build(),
        ),
        ("c", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, mut receiver) =
        create_service_directory_entry::<fcrunner::ComponentRunnerMarker>();
    let universe = RoutingTestBuilder::new("a", components)
        // Component "a" exposes a runner service.
        .add_outgoing_path("b", CapabilityPath::try_from("/svc/runner").unwrap(), runner_service)
        .build()
        .await;

    join!(
        // Bind "c:0". We expect to see a call to our runner service for the new component.
        async move {
            universe.bind_instance(&vec!["c:0"].into()).await.unwrap();
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
///    \
///     b
///      \
///       c
///
/// a: declares runner "elf" as service "/svc/runner" from self.
/// a: registers runner "elf" from realm in environment as "hobbit".
/// b: creates environment extending from realm.
/// c: uses runner "hobbit".
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_inherited_environment() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(RunnerRegistration {
                            source_name: "elf".into(),
                            source: RegistrationSource::Self_,
                            target_name: "hobbit".into(),
                        })
                        .build(),
                )
                .runner(RunnerDecl {
                    name: "elf".into(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env").build())
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .build(),
                )
                .build(),
        ),
        ("c", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, mut receiver) =
        create_service_directory_entry::<fcrunner::ComponentRunnerMarker>();
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

///  a
///   \
///    b
///
/// a: declares runner "elf" with service "/svc/runner" from "self".
/// a: registers runner "elf" from self in environment as "hobbit".
/// b: uses runner "hobbit". Fails because "hobbit" was not in environment.
#[fuchsia_async::run_singlethreaded(test)]
async fn use_runner_from_environment_not_found() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(RunnerRegistration {
                            source_name: "elf".into(),
                            source: RegistrationSource::Self_,
                            target_name: "dwarf".into(),
                        })
                        .build(),
                )
                .runner(RunnerDecl {
                    name: "elf".into(),
                    source: RunnerSource::Self_,
                    source_path: CapabilityPath::try_from("/svc/runner").unwrap(),
                })
                .build(),
        ),
        ("b", ComponentDeclBuilder::new_empty_component().use_runner("hobbit").build()),
    ];

    // Set up the system.
    let (runner_service, _receiver) =
        create_service_directory_entry::<fcrunner::ComponentRunnerMarker>();
    let universe = RoutingTestBuilder::new("a", components)
        // Component "a" exposes a runner service.
        .add_outgoing_path("a", CapabilityPath::try_from("/svc/runner").unwrap(), runner_service)
        .build()
        .await;

    // Bind "b:0". We expect it to fail because routing failed.
    //
    // The error we get is UseFromRealmNotFound we fall back to searching for an `offer`
    // declaration in the parent. Once we remove the fallback, this could be a UseFromEnvironment
    // error.
    assert_matches!(
        universe.bind_instance(&vec!["b:0"].into()).await,
        Err(ModelError::RoutingError {
            err: RoutingError::UseFromEnvironmentNotFound {
                moniker,
                capability_type,
                capability_id,
            }
        })
        if moniker == AbsoluteMoniker::from(vec!["b:0"]) &&
        capability_type == "runner".to_string() &&
        capability_id == "hobbit".to_string());
}

// TODO: Write a test for environment that extends from None. Currently, this is not
// straightforward because resolver routing is not implemented yet, which makes it impossible to
// register a new resolver and have it be usable.

#[fuchsia_async::run_singlethreaded(test)]
async fn expose_from_self_and_child() {
    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Child("c".to_string()),
                    source_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("hippo_bar_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Child("c".to_string()),
                    source_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("hippo_bar_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::Directory {
            path: "/hippo_bar_data".try_into().unwrap(),
            file: PathBuf::from("hippo"),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::Protocol {
            path: "/hippo_bar_svc".try_into().unwrap(),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory {
            path: "/hippo_data".try_into().unwrap(),
            file: PathBuf::from("hippo"),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::Protocol {
            path: "/hippo_svc".try_into().unwrap(),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn use_not_exposed() {
    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
        (
            "c",
            ComponentDeclBuilder::new()
                .directory(DirectoryDeclBuilder::new("foo_data").build())
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_data").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("hippo_data").unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(*rights::READ_RIGHTS),
                    subdir: None,
                }))
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("hippo_svc").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    // Capability is only exposed from "c", so it only be usable from there.

    // When trying to open a capability that's not exposed to realm, there's no node for it in the
    // exposed dir, so no routing takes place.  Hence we don't expect an epitaph.
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::Directory {
            path: "/hippo_data".try_into().unwrap(),
            file: PathBuf::from("hippo"),
            expected_res: ExpectedResult::ErrWithNoEpitaph,
        },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0"].into(),
        CheckUse::Protocol {
            path: "/hippo_svc".try_into().unwrap(),
            expected_res: ExpectedResult::ErrWithNoEpitaph,
        },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::Directory {
            path: "/hippo_data".try_into().unwrap(),
            file: PathBuf::from("hippo"),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use_exposed_dir(
        vec!["b:0", "c:0"].into(),
        CheckUse::Protocol {
            path: "/hippo_svc".try_into().unwrap(),
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
}

///   a
///    \
///    [b]
///      \
///       c
///
/// a: offers service /svc/foo from self
/// [b]: offers service /svc/foo to c
/// [b]: is destroyed
/// c: uses service /svc/foo, which should fail
#[fuchsia_async::run_singlethreaded(test)]
async fn use_with_destroyed_parent() {
    let use_decl = UseDecl::Protocol(UseProtocolDecl {
        source: UseSource::Parent,
        source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
    });
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Realm").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.Realm").unwrap(),
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target: OfferTarget::Collection("coll".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_transient_collection("coll")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("foo_svc").unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("c")
                .build(),
        ),
        ("c", ComponentDeclBuilder::new().use_(use_decl.clone()).build()),
    ];
    let test = RoutingTest::new("a", components).await;
    test.create_dynamic_child(
        vec![].into(),
        "coll",
        ChildDecl {
            name: "b".to_string(),
            url: "test:///b".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
        },
    )
    .await;

    // Confirm we can use service from "c".
    test.check_use(
        vec!["coll:b:1", "c:0"].into(),
        CheckUse::Protocol { path: default_service_capability(), expected_res: ExpectedResult::Ok },
    )
    .await;

    // Destroy "b", but preserve a reference to "c" so we can route from it below.
    let moniker = vec!["coll:b:1", "c:0"].into();
    let realm_c = test.model.look_up_realm(&moniker).await.expect("failed to look up realm b");
    test.destroy_dynamic_child(vec![].into(), "coll", "b").await;

    // Now attempt to route the service from "c". Should fail because "b" does not exist so we
    // cannot follow it.
    let (_client, mut server) = zx::Channel::create().unwrap();
    let err = routing::route_use_capability(
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
        "hippo".to_string(),
        &use_decl,
        &realm_c,
        &mut server,
    )
    .await
    .expect_err("routing unexpectedly succeeded");
    assert_eq!(
        format!("{:?}", err),
        format!("{:?}", ModelError::instance_not_found(vec!["coll:b:1"].into()))
    );
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
            .use_(UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Parent,
                source_path: CapabilityNameOrPath::try_from("/invalid").unwrap(),
                target_path: CapabilityPath::try_from("/svc/valid").unwrap(),
            }))
            .build(),
    )];

    // Try and use the service. We expect a failure.
    let universe = RoutingTest::new("a", components).await;
    universe
        .check_use(
            vec![].into(),
            CheckUse::Protocol {
                path: CapabilityPath::try_from("/svc/valid").unwrap(),
                expected_res: ExpectedResult::ErrWithNoEpitaph,
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
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source_path: CapabilityNameOrPath::try_from("/invalid").unwrap(),
                    source: OfferServiceSource::Parent,
                    target_path: CapabilityNameOrPath::try_from("/svc/valid").unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::try_from("/svc/valid").unwrap(),
                    target_path: CapabilityPath::try_from("/svc/valid").unwrap(),
                }))
                .build(),
        ),
    ];

    // Try and use the service. We expect a failure.
    let universe = RoutingTest::new("a", components).await;
    universe
        .check_use(
            vec!["b:0"].into(),
            CheckUse::Protocol {
                path: CapabilityPath::try_from("/svc/valid").unwrap(),
                expected_res: ExpectedResult::ErrWithNoEpitaph,
            },
        )
        .await;
}

///   a
///    \
///     b
///
/// b: uses framework events "started", and "capability_requested"
#[fuchsia_async::run_singlethreaded(test)]
async fn use_event_from_framework() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "capability_requested".into(),
                    target_name: "capability_requested".into(),
                    filter: None,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "started".into(),
                    target_name: "started".into(),
                    filter: None,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "resolved".into(),
                    target_name: "resolved".into(),
                    filter: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Event {
            names: vec!["capability_requested".into(), "started".into()],
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
}

///   a
///    \
///     b
///
/// a; attempts to offer event "capability_requested" to b.
#[fuchsia_async::run_singlethreaded(test)]
async fn cannot_offer_capability_requested_event() {
    let components = vec![(
        "a",
        ComponentDeclBuilder::new()
            .offer(OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferServiceSource::Parent,
                source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                target_path: "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                target: OfferTarget::Child("b".to_string()),
                dependency_type: DependencyType::Strong,
            }))
            .offer(OfferDecl::Event(OfferEventDecl {
                source: OfferEventSource::Framework,
                source_name: "capability_requested".into(),
                target_name: "capability_requested_on_a".into(),
                target: OfferTarget::Child("b".to_string()),
                filter: None,
            }))
            .add_lazy_child("b")
            .build(),
    )];
    let test = RoutingTest::new("a", components).await;
    assert_matches!(
        test.bind_instance(&vec!["a:0", "b:0"].into()).await,
        Err(ModelError::ResolverError { err: ResolverError::ManifestInvalid { .. } })
    );
}

/// a; attempts to use event "capability_requested" from realm and fails.
#[fuchsia_async::run_singlethreaded(test)]
async fn cannot_use_capability_requested_event_from_realm() {
    let components = vec![(
        "a",
        ComponentDeclBuilder::new()
            .use_(UseDecl::Event(UseEventDecl {
                source: UseSource::Parent,
                source_name: "capability_requested".into(),
                target_name: "capability_requested_from_parent".into(),
                filter: None,
            }))
            .build(),
    )];
    let test = RoutingTest::new("a", components).await;
    assert_matches!(
        test.bind_instance(&vec!["a:0"].into()).await,
        Err(ModelError::ResolverError { err: ResolverError::ManifestInvalid { .. } })
    );
}

///   a
///    \
///     b
///
/// a: uses framework event "started" and offers to b as "started_on_a"
/// b: uses framework event "started_on_a" as "started"
#[fuchsia_async::run_singlethreaded(test)]
async fn use_event_from_parent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Framework,
                    source_name: "started".into(),
                    target_name: "started_on_a".into(),
                    target: OfferTarget::Child("b".to_string()),
                    filter: None,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "started_on_a".into(),
                    target_name: "started".into(),
                    filter: None,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "resolved".into(),
                    target_name: "resolved".into(),
                    filter: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Event { names: vec!["started".into()], expected_res: ExpectedResult::Ok },
    )
    .await;
}

///   a
///    \
///     b
///      \
///       c
///
/// a: uses framework event "started" and offers to b as "started_on_a"
/// a: uses framework event "stopped" and offers to b as "stopped_on_a"
/// b: offers realm event "started_on_a" to c
/// b: offers realm event "destroyed" from framework
/// c: uses realm event "started_on_a"
/// c: uses realm event "destroyed"
/// c: uses realm event "stopped_on_a" but fails to do so
#[fuchsia_async::run_singlethreaded(test)]
async fn use_event_from_grandparent() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Framework,
                    source_name: "started".into(),
                    target_name: "started_on_a".into(),
                    target: OfferTarget::Child("b".to_string()),
                    filter: None,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Framework,
                    source_name: "stopped".into(),
                    target_name: "stopped_on_b".into(),
                    target: OfferTarget::Child("b".to_string()),
                    filter: None,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Parent,
                    source_name: "started_on_a".into(),
                    target_name: "started_on_a".into(),
                    target: OfferTarget::Child("c".to_string()),
                    filter: None,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Framework,
                    source_name: "destroyed".into(),
                    target_name: "destroyed".into(),
                    target: OfferTarget::Child("c".to_string()),
                    filter: Some(hashmap!{"path".to_string() => DictionaryValue::Str("/diagnostics".to_string())}),
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "started_on_a".into(),
                    target_name: "started".into(),
                    filter: None,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "destroyed".into(),
                    target_name: "destroyed".into(),
                    filter: Some(hashmap!{"path".to_string() => DictionaryValue::Str("/diagnostics".to_string())}),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "stopped_on_a".into(),
                    target_name: "stopped".into(),
                    filter: None,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "resolved".into(),
                    target_name: "resolved".into(),
                    filter: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Event {
            names: vec!["started".into(), "destroyed".into()],
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Event { names: vec!["stopped".into()], expected_res: ExpectedResult::Err },
    )
    .await;
}

///   a
///   |
///   b
///  / \
/// c   d
///
/// a: offer framework event "capability_ready" with filters "/foo", "/bar", "/baz" to b
/// b: uses realm event "capability_ready" with filters "/foo"
/// b: offers realm event "capabilty_ready" with filters "/foo", "/bar" to c, d
/// c: uses realm event "capability_ready" with filters "/foo", "/bar"
/// d: uses realm event "capability_ready" with filters "/baz" (fails)
#[fuchsia_async::run_singlethreaded(test)]
async fn event_filter_routing() {
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Framework,
                    source_name: "capability_ready".into(),
                    target_name: "capability_ready".into(),
                    target: OfferTarget::Child("b".to_string()),
                    filter: Some(hashmap! {
                        "path".to_string() => DictionaryValue::StrVec(vec![
                            "/foo".to_string(), "/bar".to_string(), "/baz".to_string()
                        ])
                    }),
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "capability_ready".into(),
                    target_name: "capability_ready_foo".into(),
                    filter: Some(hashmap! {
                        "path".to_string() => DictionaryValue::Str("/foo".into()),
                    }),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "resolved".into(),
                    target_name: "resolved".into(),
                    filter: None,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target: OfferTarget::Child("d".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Parent,
                    source_name: "capability_ready".into(),
                    target_name: "capability_ready".into(),
                    target: OfferTarget::Child("c".to_string()),
                    filter: Some(hashmap! {
                        "path".to_string() => DictionaryValue::StrVec(vec![
                            "/foo".to_string(), "/bar".to_string()
                        ])
                    }),
                }))
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferEventSource::Parent,
                    source_name: "capability_ready".into(),
                    target_name: "capability_ready".into(),
                    target: OfferTarget::Child("d".to_string()),
                    filter: Some(hashmap! {
                        "path".to_string() => DictionaryValue::StrVec(vec![
                            "/foo".to_string(), "/bar".to_string()
                        ])
                    }),
                }))
                .add_lazy_child("c")
                .add_lazy_child("d")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "capability_ready".into(),
                    target_name: "capability_ready_foo_bar".into(),
                    filter: Some(hashmap! {
                        "path".to_string() => DictionaryValue::StrVec(vec![
                            "/foo".to_string(), "/bar".to_string()
                        ])
                    }),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "resolved".into(),
                    target_name: "resolved".into(),
                    filter: None,
                }))
                .build(),
        ),
        (
            "d",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: "fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.BlockingEventSource".try_into().unwrap(),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "capability_ready".into(),
                    target_name: "capability_ready_baz".into(),
                    filter: Some(hashmap! {
                        "path".to_string() => DictionaryValue::Str("/baz".into()),
                    }),
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "resolved".into(),
                    target_name: "resolved".into(),
                    filter: None,
                }))
                .build(),
        ),
    ];
    let test = RoutingTest::new("a", components).await;
    test.check_use(
        vec!["b:0"].into(),
        CheckUse::Event {
            names: vec!["capability_ready_foo".into()],
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "c:0"].into(),
        CheckUse::Event {
            names: vec!["capability_ready_foo_bar".into()],
            expected_res: ExpectedResult::Ok,
        },
    )
    .await;
    test.check_use(
        vec!["b:0", "d:0"].into(),
        CheckUse::Event {
            names: vec!["capability_ready_baz".into()],
            expected_res: ExpectedResult::Err,
        },
    )
    .await;
}
