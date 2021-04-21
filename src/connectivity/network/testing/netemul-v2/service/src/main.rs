// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _},
    fidl::endpoints::{DiscoverableService, ServerEnd},
    fidl_fuchsia_logger as flogger,
    fidl_fuchsia_netemul::{
        self as fnetemul, ChildDef, ChildUses, ManagedRealmMarker, ManagedRealmRequest,
        RealmOptions, SandboxRequest, SandboxRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    futures::{channel::mpsc, FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _},
    log::{debug, error, info},
    pin_utils::pin_mut,
    std::collections::HashMap,
    std::sync::atomic::{AtomicU64, Ordering},
};

type Result<T = (), E = anyhow::Error> = std::result::Result<T, E>;

const REALM_COLLECTION_NAME: &str = "netemul";

struct ManagedRealm {
    server_end: ServerEnd<ManagedRealmMarker>,
    realm: fuchsia_component_test::RealmInstance,
}

impl ManagedRealm {
    async fn create(
        server_end: ServerEnd<ManagedRealmMarker>,
        options: RealmOptions,
        prefix: &str,
    ) -> Result<ManagedRealm> {
        use fuchsia_component_test::{
            builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
            Moniker,
        };

        let RealmOptions { name, children, .. } = options;
        let mut exposed_services = HashMap::new();
        let mut components_using_all = Vec::new();
        let mut builder = RealmBuilder::new().await.context("error creating new realm builder")?;
        for ChildDef { url, name, exposes, uses, .. } in children.unwrap_or_default() {
            let url = url.ok_or(anyhow!("no url provided"))?;
            let name = name.ok_or(anyhow!("no name provided"))?;
            let _: &mut RealmBuilder = builder
                .add_component(name.as_ref(), ComponentSource::url(&url))
                .await
                .with_context(|| {
                    format!("error adding new component with name '{}' and url '{}'", name, url)
                })?;
            if let Some(exposes) = exposes {
                for exposed in exposes {
                    // TODO(https://fxbug.dev/72043): allow duplicate services.
                    // Service names will be aliased as `child_name/service_name`, and this panic
                    // will be replaced with an INVALID_ARGS epitaph sent on the `ManagedRealm`
                    // channel if a child component with a duplicate name is created.
                    match exposed_services.entry(exposed) {
                        std::collections::hash_map::Entry::Occupied(entry) => {
                            panic!(
                                "duplicate service name '{}' exposed from component '{}'",
                                entry.key(),
                                entry.get(),
                            );
                        }
                        std::collections::hash_map::Entry::Vacant(entry) => {
                            let _: &mut RealmBuilder = builder
                                .add_route(CapabilityRoute {
                                    capability: Capability::protocol(entry.key()),
                                    source: RouteEndpoint::component(&name),
                                    targets: vec![RouteEndpoint::AboveRoot],
                                })
                                .with_context(|| {
                                    format!(
                                        "error adding route exposing capability '{}' from \
                                        component '{}'",
                                        entry.key(),
                                        name,
                                    )
                                })?;
                            let _: &mut String = entry.insert(name.clone());
                        }
                    }
                }
            }
            if let Some(uses) = uses {
                match uses {
                    ChildUses::All(fnetemul::Empty {}) => {
                        // Route all built-in netemul services to the child.
                        // TODO(https://fxbug.dev/72992): route netemul-provided `/dev`.
                        // TODO(https://fxbug.dev/72403): route netemul-provided `SyncManager`.
                        // TODO(https://fxbug.dev/72402): route netemul-provided `NetworkContext`.
                        let _: &mut RealmBuilder = builder
                            .add_route(CapabilityRoute {
                                capability: Capability::protocol(
                                    flogger::LogSinkMarker::SERVICE_NAME,
                                ),
                                source: RouteEndpoint::AboveRoot,
                                targets: vec![RouteEndpoint::component(&name)],
                            })
                            .with_context(|| {
                                format!(
                                    "error adding route exposing '{}' to component '{}'",
                                    flogger::LogSinkMarker::SERVICE_NAME,
                                    name
                                )
                            })?;
                        let () = components_using_all.push(name);
                    }
                    ChildUses::Capabilities(caps) => {
                        for cap in caps {
                            match cap {
                                fnetemul::Capability::LogSink(fnetemul::Empty {}) => {
                                    let _: &mut RealmBuilder = builder
                                        .add_route(CapabilityRoute {
                                            capability: Capability::protocol(
                                                flogger::LogSinkMarker::SERVICE_NAME,
                                            ),
                                            source: RouteEndpoint::AboveRoot,
                                            targets: vec![RouteEndpoint::component(&name)],
                                        })
                                        .with_context(|| {
                                            format!(
                                                "error adding route exposing '{}' to component \
                                                '{}'",
                                                flogger::LogSinkMarker::SERVICE_NAME,
                                                name,
                                            )
                                        })?;
                                }
                                _ => todo!(),
                            }
                        }
                    }
                }
            }
        }
        for component in components_using_all {
            for (service, source) in &exposed_services {
                // Don't route a capability back to its source.
                if &component == source {
                    continue;
                }
                let _: &mut RealmBuilder = builder
                    .add_route(CapabilityRoute {
                        capability: Capability::protocol(service),
                        source: RouteEndpoint::component(source),
                        targets: vec![RouteEndpoint::component(&component)],
                    })
                    .with_context(|| {
                        format!(
                            "error adding route exposing '{}' from component '{}' to component \
                            '{}'",
                            service, source, component,
                        )
                    })?;
            }
        }
        let mut realm = builder.build();
        // Mark all dependencies between components in the test realm as weak, to allow for
        // dependency cycles.
        //
        // TODO(https://fxbug.dev/74977): once we can specify weak dependencies directly with the
        // RealmBuilder API, only mark dependencies as `weak` that originated from a `ChildUses.all`
        // configuration.
        let cm_rust::ComponentDecl { offers, .. } = realm.get_decl_mut(&Moniker::root())?;
        for offer in offers {
            match offer {
                cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    dependency_type,
                    ..
                }) => {
                    *dependency_type = cm_rust::DependencyType::WeakForMigration;
                }
                offer => error!(
                    "there should only be protocol offers from the root of the managed realm; \
                    found {:?}",
                    offer
                ),
            }
        }
        let name =
            name.map(|name| format!("{}-{}", prefix, name)).unwrap_or_else(|| prefix.to_string());
        info!("creating new ManagedRealm with name '{}'", name);
        let () = realm.set_collection_name(REALM_COLLECTION_NAME);
        let realm = realm.create_with_name(name).await.context("error creating realm instance")?;
        Ok(ManagedRealm { server_end, realm })
    }

    async fn run_service(self) -> Result {
        let Self { server_end, realm } = self;
        let mut stream = server_end.into_stream().context("failed to acquire request stream")?;
        while let Some(request) = stream.try_next().await.context("FIDL error")? {
            match request {
                ManagedRealmRequest::GetMoniker { responder } => {
                    let moniker =
                        format!("{}\\:{}", REALM_COLLECTION_NAME, realm.root.child_name());
                    let () = responder.send(&moniker).context("FIDL error")?;
                }
                ManagedRealmRequest::ConnectToService {
                    service_name,
                    child_name,
                    req,
                    control_handle: _,
                } => {
                    // TODO(https://fxbug.dev/72043): allow `child_name` to be specified once we
                    // prefix capabilities with the name of the component exposing them.
                    //
                    // Currently `child_name` isn't used to disambiguate duplicate services, so we
                    // don't allow it to be specified.
                    if let Some(_) = child_name {
                        todo!("allow `child_name` to be specified in `ConnectToService` request");
                    }
                    debug!(
                        "connecting to service `{}` exposed by child `{:?}`",
                        service_name, child_name
                    );
                    let () = realm
                        .root
                        .connect_request_to_named_service_at_exposed_dir(&service_name, req)
                        .with_context(|| {
                            format!("failed to open protocol {} in directory", service_name)
                        })?;
                }
                ManagedRealmRequest::AddDevice { path: _, device: _, responder: _ } => todo!(),
                ManagedRealmRequest::RemoveDevice { path: _, responder: _ } => todo!(),
            }
        }
        Ok(())
    }
}

async fn handle_sandbox(
    stream: SandboxRequestStream,
    sandbox_name: impl std::fmt::Display,
) -> Result<(), fidl::Error> {
    let (tx, rx) = mpsc::channel(1);
    let realm_index = AtomicU64::new(0);
    let sandbox_fut = stream.try_for_each_concurrent(None, |request| {
        let mut tx = tx.clone();
        let sandbox_name = &sandbox_name;
        let realm_index = &realm_index;
        async move {
            match request {
                // TODO(https://fxbug.dev/72253): send the correct epitaph on failure.
                SandboxRequest::CreateRealm { realm, options, control_handle: _ } => {
                    let index = realm_index.fetch_add(1, Ordering::SeqCst);
                    let prefix = format!("{}{}", sandbox_name, index);
                    match ManagedRealm::create(realm, options, &prefix)
                        .await
                        .context("failed to create ManagedRealm")
                    {
                        Ok(realm) => tx.send(realm).await.expect("receiver should not be closed"),
                        Err(err) => error!("error creating ManagedRealm: {:?}", err),
                    }
                }
                SandboxRequest::GetNetworkContext { network_context: _, control_handle: _ } => {
                    todo!()
                }
                SandboxRequest::GetSyncManager { sync_manager: _, control_handle: _ } => todo!(),
            }
            Ok(())
        }
    });
    let realms_fut = rx
        .for_each_concurrent(None, |realm| async {
            realm
                .run_service()
                .await
                .unwrap_or_else(|e| error!("error running ManagedRealm service: {:?}", e))
        })
        .fuse();
    pin_mut!(sandbox_fut, realms_fut);
    futures::select! {
        result = sandbox_fut => result,
        () = realms_fut => unreachable!("realms_fut should never complete"),
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result {
    let () = fuchsia_syslog::init().context("cannot init logger")?;
    info!("starting...");

    let mut fs = ServiceFs::new_local();
    let _: &mut ServiceFsDir<'_, _> = fs.dir("svc").add_fidl_service(|s: SandboxRequestStream| s);
    let _: &mut ServiceFs<_> = fs.take_and_serve_directory_handle()?;

    let sandbox_index = AtomicU64::new(0);
    let () = fs
        .for_each_concurrent(None, |stream| async {
            let index = sandbox_index.fetch_add(1, Ordering::SeqCst);
            handle_sandbox(stream, index)
                .await
                .unwrap_or_else(|e| error!("error handling SandboxRequestStream: {:?}", e))
        })
        .await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl::endpoints::Proxy as _, fidl_fuchsia_netemul as fnetemul,
        fidl_fuchsia_netemul_test::CounterMarker, fuchsia_zircon as zx, std::convert::TryFrom as _,
    };

    // We can't just use a counter for the sandbox identifier, as we do in `main`, because tests
    // each run in separate processes, but use the same backing collection of components created
    // through `RealmBuilder`. If we used a counter, it wouldn't be shared across processes, and
    // would cause name collisions between the `RealmInstance` monikers.
    fn setup_sandbox_service(
        sandbox_name: &str,
    ) -> (fnetemul::SandboxProxy, impl futures::Future<Output = ()> + '_) {
        let (sandbox_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<fnetemul::SandboxMarker>()
                .expect("failed to create SandboxProxy");
        (sandbox_proxy, async move {
            handle_sandbox(stream, sandbox_name).await.expect("handle_sandbox error")
        })
    }

    struct TestRealm {
        realm: fnetemul::ManagedRealmProxy,
    }

    impl TestRealm {
        fn new(sandbox: &fnetemul::SandboxProxy, options: fnetemul::RealmOptions) -> TestRealm {
            let (realm, server) = fidl::endpoints::create_proxy::<fnetemul::ManagedRealmMarker>()
                .expect("failed to create ManagedRealmProxy");
            let () = sandbox
                .create_realm(server, options)
                .expect("fuchsia.netemul/Sandbox.create_realm call failed");
            TestRealm { realm }
        }

        fn connect_to_service<S: DiscoverableService>(&self) -> S::Proxy {
            let (proxy, server) = zx::Channel::create().expect("failed to create zx::Channel");
            let () = self
                .realm
                .connect_to_service(S::SERVICE_NAME, None, server)
                .with_context(|| format!("{}", S::SERVICE_NAME))
                .expect("failed to connect");
            let proxy = fasync::Channel::from_channel(proxy)
                .expect("failed to create fasync::Channel from zx::Channel");
            S::Proxy::from_channel(proxy)
        }
    }

    async fn with_sandbox<F, Fut>(name: &str, test: F)
    where
        F: FnOnce(fnetemul::SandboxProxy) -> Fut,
        Fut: futures::Future<Output = ()>,
    {
        let () = fuchsia_syslog::init().expect("cannot init logger");
        let () = fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);
        info!("starting test...");
        let (sandbox, fut) = setup_sandbox_service(name);
        let ((), ()) = futures::future::join(fut, test(sandbox)).await;
    }

    const TEST_DRIVER_COMPONENT_NAME: &str = "test_driver";
    const COUNTER_COMPONENT_NAME: &str = "counter";
    const COUNTER_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.com/netemul-v2-tests#meta/counter.cm";

    #[fasync::run_singlethreaded(test)]
    async fn can_connect_to_single_service() {
        with_sandbox("can_connect_to_single_service", |sandbox| async move {
            let realm = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let counter = realm.connect_to_service::<CounterMarker>();
            assert_eq!(
                counter
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                1,
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn multiple_realms() {
        with_sandbox("multiple_realms", |sandbox| async move {
            let realm_a = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    name: Some("a".to_string()),
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let realm_b = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    name: Some("b".to_string()),
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let counter_a = realm_a.connect_to_service::<CounterMarker>();
            let counter_b = realm_b.connect_to_service::<CounterMarker>();
            assert_eq!(
                counter_a
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                1,
            );
            for i in 1..=10 {
                assert_eq!(
                    counter_b
                        .increment()
                        .await
                        .expect("fuchsia.netemul.test/Counter.increment call failed"),
                    i,
                );
            }
            assert_eq!(
                counter_a
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                2,
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn drop_realm_destroys_children() {
        with_sandbox("drop_realm_destroys_children", |sandbox| async move {
            let realm = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let counter = realm.connect_to_service::<CounterMarker>();
            assert_eq!(
                counter
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                1,
            );
            drop(realm);
            assert_eq!(
                fasync::OnSignals::new(
                    &counter
                        .into_channel()
                        .expect("failed to convert `CounterProxy` into `fasync::Channel`"),
                    zx::Signals::CHANNEL_PEER_CLOSED,
                )
                .await,
                Ok(zx::Signals::CHANNEL_PEER_CLOSED),
                "`CounterProxy` should be closed when `ManagedRealmProxy` is dropped",
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn drop_sandbox_destroys_realms() {
        with_sandbox("drop_sandbox_destroys_realms", |sandbox| async move {
            const REALMS_COUNT: usize = 10;
            let realms = std::iter::repeat(())
                .take(REALMS_COUNT)
                .map(|()| {
                    TestRealm::new(
                        &sandbox,
                        fnetemul::RealmOptions {
                            children: Some(vec![fnetemul::ChildDef {
                                url: Some(COUNTER_PACKAGE_URL.to_string()),
                                name: Some(COUNTER_COMPONENT_NAME.to_string()),
                                exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                                ])),
                                ..fnetemul::ChildDef::EMPTY
                            }]),
                            ..fnetemul::RealmOptions::EMPTY
                        },
                    )
                })
                .collect::<Vec<_>>();

            let mut counters = vec![];
            for realm in &realms {
                let counter = realm.connect_to_service::<CounterMarker>();
                assert_eq!(
                    counter
                        .increment()
                        .await
                        .expect("fuchsia.netemul.test/Counter.increment call failed"),
                    1,
                );
                let () = counters.push(counter);
            }
            drop(sandbox);
            for counter in counters {
                assert_eq!(
                    fasync::OnSignals::new(
                        &counter
                            .into_channel()
                            .expect("failed to convert `CounterProxy` into `fasync::Channel`"),
                        zx::Signals::CHANNEL_PEER_CLOSED,
                    )
                    .await,
                    Ok(zx::Signals::CHANNEL_PEER_CLOSED),
                    "`CounterProxy` should be closed when `SandboxProxy` is dropped",
                );
            }
            for realm in realms {
                let TestRealm { realm } = realm;
                assert_eq!(
                    fasync::OnSignals::new(
                        &realm
                            .into_channel()
                            .expect("failed to convert `ManagedRealmProxy` into `fasync::Channel`"),
                        zx::Signals::CHANNEL_PEER_CLOSED,
                    )
                    .await,
                    Ok(zx::Signals::CHANNEL_PEER_CLOSED),
                    "`ManagedRealmProxy` should be closed when `SandboxProxy` is dropped",
                );
            }
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_realm_name() {
        with_sandbox("set_realm_name", |sandbox| async move {
            let TestRealm { realm } = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    name: Some("test-realm-name".to_string()),
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            assert_eq!(
                realm
                    .get_moniker()
                    .await
                    .expect("fuchsia.netemul/ManagedRealm.get_moniker call failed"),
                format!("{}\\:set_realm_name0-test-realm-name", REALM_COLLECTION_NAME),
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_generated_realm_name() {
        with_sandbox("auto_generated_realm_name", |sandbox| async move {
            const REALMS_COUNT: usize = 10;
            for i in 0..REALMS_COUNT {
                let TestRealm { realm } = TestRealm::new(
                    &sandbox,
                    fnetemul::RealmOptions {
                        name: None,
                        children: Some(vec![fnetemul::ChildDef {
                            url: Some(COUNTER_PACKAGE_URL.to_string()),
                            name: Some(COUNTER_COMPONENT_NAME.to_string()),
                            exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                            uses: Some(fnetemul::ChildUses::Capabilities(vec![
                                fnetemul::Capability::LogSink(fnetemul::Empty {}),
                            ])),
                            ..fnetemul::ChildDef::EMPTY
                        }]),
                        ..fnetemul::RealmOptions::EMPTY
                    },
                );
                assert_eq!(
                    realm
                        .get_moniker()
                        .await
                        .expect("fuchsia.netemul/ManagedRealm.get_moniker call failed"),
                    format!("{}\\:auto_generated_realm_name{}", REALM_COLLECTION_NAME, i),
                );
            }
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect() {
        with_sandbox("inspect", |sandbox| async move {
            const REALMS_COUNT: usize = 10;
            let realms = std::iter::repeat(())
                .take(REALMS_COUNT)
                .map(|()| {
                    TestRealm::new(
                        &sandbox,
                        fnetemul::RealmOptions {
                            children: Some(vec![fnetemul::ChildDef {
                                url: Some(COUNTER_PACKAGE_URL.to_string()),
                                name: Some(COUNTER_COMPONENT_NAME.to_string()),
                                exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                                uses: Some(fnetemul::ChildUses::Capabilities(vec![
                                    fnetemul::Capability::LogSink(fnetemul::Empty {}),
                                ])),
                                ..fnetemul::ChildDef::EMPTY
                            }]),
                            ..fnetemul::RealmOptions::EMPTY
                        },
                    )
                })
                // Collect the `TestRealm`s because we want all the test realms to be alive for the
                // duration of the test.
                //
                // Each `TestRealm` owns a `ManagedRealmProxy`, which has RAII semantics: when the
                // proxy is dropped, the backing test realm managed by the sandbox is also
                // destroyed.
                .collect::<Vec<_>>();
            for (i, realm) in realms.iter().enumerate() {
                let i = u32::try_from(i).unwrap();
                let counter = realm.connect_to_service::<CounterMarker>();
                for j in 1..=i {
                    assert_eq!(
                        counter.increment().await.expect(&format!(
                            "fuchsia.netemul.test/Counter.increment call failed on realm {}",
                            i
                        )),
                        j,
                    );
                }
                let TestRealm { realm } = realm;
                let selector = vec![
                    TEST_DRIVER_COMPONENT_NAME.into(),
                    realm.get_moniker().await.expect(&format!(
                        "fuchsia.netemul/ManagedRealm.get_moniker call failed on realm {}",
                        i
                    )),
                    COUNTER_COMPONENT_NAME.into(),
                ];
                let data = diagnostics_reader::ArchiveReader::new()
                    .add_selector(diagnostics_reader::ComponentSelector::new(selector))
                    .snapshot::<diagnostics_reader::Inspect>()
                    .await
                    .expect(&format!("failed to get inspect data in realm {}", i))
                    .into_iter()
                    .map(
                        |diagnostics_data::InspectData {
                             data_source: _,
                             metadata: _,
                             moniker: _,
                             payload,
                             version: _,
                         }| payload,
                    )
                    .collect::<Vec<_>>();
                match &data[..] {
                    [datum] => match datum {
                        None => panic!("empty inspect payload in realm {}", i),
                        Some(data) => {
                            fuchsia_inspect::assert_inspect_tree!(data, root: {
                                counter: {
                                    count: u64::from(i),
                                }
                            });
                        }
                    },
                    data => panic!(
                        "there should be exactly one matching inspect node in realm {}; got {:?}",
                        i, data
                    ),
                }
            }
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn child_uses_all_capabilities() {
        // These services are aliased instances of the `fuchsia.netemul.test.Counter` service
        // (configured in the component manifest), so there is no actual `CounterAMarker` type, for
        // example, from which we could extract its `SERVICE_NAME`.
        const COUNTER_A_SERVICE_NAME: &str = "fuchsia.netemul.test.CounterA";
        const COUNTER_B_SERVICE_NAME: &str = "fuchsia.netemul.test.CounterB";

        with_sandbox("child_uses_all_capabilities", |sandbox| async move {
            let TestRealm { realm } = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(vec![
                        fnetemul::ChildDef {
                            url: Some(COUNTER_PACKAGE_URL.to_string()),
                            name: Some("counter-a".to_string()),
                            exposes: Some(vec![COUNTER_A_SERVICE_NAME.to_string()]),
                            uses: Some(fnetemul::ChildUses::All(fnetemul::Empty {})),
                            ..fnetemul::ChildDef::EMPTY
                        },
                        fnetemul::ChildDef {
                            url: Some(COUNTER_PACKAGE_URL.to_string()),
                            name: Some("counter-b".to_string()),
                            exposes: Some(vec![COUNTER_B_SERVICE_NAME.to_string()]),
                            uses: Some(fnetemul::ChildUses::All(fnetemul::Empty {})),
                            ..fnetemul::ChildDef::EMPTY
                        },
                        // TODO(https://fxbug.dev/74868): once we can allow the ERROR logs that
                        // result from the routing failure, add a child that does *not* use `All`,
                        // and verify that it does not have access to the other components' exposed
                        // services.
                    ]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let counter_b = {
                let (counter_b, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
                    .expect("failed to create CounterB proxy");
                let () = realm
                    .connect_to_service(COUNTER_B_SERVICE_NAME, None, server_end.into_channel())
                    .expect("failed to connect to CounterB service");
                counter_b
            };
            // counter-b should have access to counter-a's exposed service.
            let (counter_a, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
                .expect("failed to create CounterA proxy");
            let () = counter_b
                .connect_to_service(COUNTER_A_SERVICE_NAME, server_end.into_channel())
                .expect("fuchsia.netemul.test/CounterB.connect_to_service call failed");
            assert_eq!(
                counter_a
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/CounterA.increment call failed"),
                1,
            );
            // counter-a should have access to counter-b's exposed service.
            let (counter_b, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
                .expect("failed to create CounterA proxy");
            let () = counter_a
                .connect_to_service(COUNTER_B_SERVICE_NAME, server_end.into_channel())
                .expect("fuchsia.netemul.test/CounterA.connect_to_service call failed");
            assert_eq!(
                counter_b
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/CounterB.increment call failed"),
                1,
            );
        })
        .await
    }
}
