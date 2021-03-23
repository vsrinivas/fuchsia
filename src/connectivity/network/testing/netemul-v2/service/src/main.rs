// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_netemul::{
        self as fnetemul, ChildDef, ChildUses, ManagedRealmMarker, ManagedRealmRequest,
        RealmOptions, SandboxRequest, SandboxRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    futures::{channel::mpsc, FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _},
    log::{debug, error, info},
    pin_utils::pin_mut,
    std::collections::HashSet,
    std::sync::atomic::{AtomicU64, Ordering},
};

type Result<T = (), E = anyhow::Error> = std::result::Result<T, E>;

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
        use fuchsia_component_test::builder::{
            Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint,
        };

        let RealmOptions { name, children, .. } = options;
        let mut exposed_services = HashSet::new();
        let mut builder = RealmBuilder::new().await.context("error creating new `RealmBuilder`")?;
        for ChildDef { url, name, exposes, uses, .. } in children.unwrap_or_default() {
            let url = url.ok_or(anyhow!("no url provided"))?;
            let name = name.ok_or(anyhow!("no name provided"))?;
            let _: &mut RealmBuilder = builder
                .add_component(name.clone(), ComponentSource::url(url.clone()))
                .await
                .with_context(|| {
                    format!("error adding new component with name '{}' and url '{}'", name, url)
                })?;
            if let Some(exposes) = exposes {
                for exposed in exposes {
                    let _: &mut RealmBuilder = builder
                        .add_route(CapabilityRoute {
                            capability: Capability::protocol(exposed.clone()),
                            source: RouteEndpoint::component(name.clone()),
                            targets: vec![RouteEndpoint::AboveRoot],
                        })
                        .with_context(|| {
                            format!(
                                "error adding route exposing capability '{}' from component '{}'",
                                exposed, name
                            )
                        })?;
                    // TODO(https://fxbug.dev/72043): allow duplicate services.
                    // Service names will be aliased as `child_name/service_name`, and this panic
                    // will be replaced with a panic if a child component with a duplicate name is
                    // created.
                    if !exposed_services.insert(exposed.clone()) {
                        panic!("duplicate service name exposed from child: {}", exposed);
                    }
                }
            }
            for used in uses {
                match used {
                    ChildUses::All(fnetemul::Empty {}) => todo!(),
                    ChildUses::Capabilities(caps) => {
                        for cap in caps {
                            match cap {
                                // TODO(https://fxbug.dev/72047): rather than exposing the real
                                // syslog to the realm, netemul-sandbox-v2 should offer a custom
                                // LogSink service to the components under test that decorates the
                                // logs with the name of the containing test realm before logging
                                // them to syslog.
                                fnetemul::Capability::LogSink(fnetemul::Empty {}) => {
                                    let _: &mut RealmBuilder =
                                        builder.add_route(CapabilityRoute {
                                            capability: Capability::protocol(
                                                "fuchsia.logger.LogSink",
                                            ),
                                            source: RouteEndpoint::AboveRoot,
                                            targets: vec![RouteEndpoint::component(name.clone())],
                                        })
                                        .with_context(|| format!(
                                            "error adding route exposing fuchsia.logger.LogSink to component '{}'", name
                                        ))?;
                                }
                                _ => todo!(),
                            }
                        }
                    }
                }
            }
        }
        let name = format!("{}-{}", prefix, name.unwrap_or("realm".to_string()));
        info!("creating new ManagedRealm with name '{}'", name);
        let realm = builder
            .build()
            .create_with_name(name)
            .await
            .context("error creating `RealmInstance`")?;
        Ok(ManagedRealm { server_end, realm })
    }

    async fn run_service(self) -> Result {
        let Self { server_end, realm } = self;
        let mut stream = server_end.into_stream().context("failed to acquire request stream")?;
        while let Some(request) = stream.try_next().await.context("FIDL error")? {
            match request {
                ManagedRealmRequest::GetMoniker { responder } => {
                    let moniker = format!(
                        "{}\\:{}",
                        fuchsia_component_test::DEFAULT_COLLECTION_NAME,
                        realm.root.child_name()
                    );
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
        fidl_fuchsia_netemul_test::CounterMarker, fuchsia_zircon as zx,
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

    fn create_test_realm(
        sandbox: &fnetemul::SandboxProxy,
        options: fnetemul::RealmOptions,
    ) -> fnetemul::ManagedRealmProxy {
        let (realm, server) = fidl::endpoints::create_proxy::<fnetemul::ManagedRealmMarker>()
            .expect("failed to create ManagedRealmProxy");
        let () = sandbox
            .create_realm(server, options)
            .expect("fuchsia.netemul/Sandbox.create_realm call failed");
        realm
    }

    fn connect_to_service<S: fidl::endpoints::DiscoverableService>(
        realm: &fnetemul::ManagedRealmProxy,
    ) -> S::Proxy {
        let (proxy, server) = zx::Channel::create().expect("failed to create zx::Channel");
        let () = realm
            .connect_to_service(S::SERVICE_NAME, None, server)
            .with_context(|| format!("{}", S::SERVICE_NAME))
            .expect("failed to connect");
        let proxy = fasync::Channel::from_channel(proxy)
            .expect("failed to create fasync::Channel from zx::Channel");
        S::Proxy::from_channel(proxy)
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

    const COUNTER_COMPONENT_NAME: &str = "counter";
    const COUNTER_SERVICE_NAME: &str =
        <CounterMarker as fidl::endpoints::DiscoverableService>::SERVICE_NAME;
    const COUNTER_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.com/netemul-v2-tests#meta/counter.cm";

    #[fasync::run_singlethreaded(test)]
    async fn can_connect_to_single_service() {
        with_sandbox("can_connect_to_single_service", |sandbox| async move {
            let realm = create_test_realm(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![COUNTER_SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let counter = connect_to_service::<CounterMarker>(&realm);
            assert_eq!(
                counter
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                1,
                "unexpected counter value",
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn multiple_realms() {
        with_sandbox("multiple_realms", |sandbox| async move {
            let realm_a = create_test_realm(
                &sandbox,
                fnetemul::RealmOptions {
                    name: Some("a".to_string()),
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![COUNTER_SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let realm_b = create_test_realm(
                &sandbox,
                fnetemul::RealmOptions {
                    name: Some("b".to_string()),
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![COUNTER_SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let counter_a = connect_to_service::<CounterMarker>(&realm_a);
            let counter_b = connect_to_service::<CounterMarker>(&realm_b);
            assert_eq!(
                counter_a
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                1,
                "unexpected counter value",
            );
            for i in 1..=10 {
                assert_eq!(
                    counter_b
                        .increment()
                        .await
                        .expect("fuchsia.netemul.test/Counter.increment call failed"),
                    i,
                    "unexpected counter value",
                );
            }
            assert_eq!(
                counter_a
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                2,
                "unexpected counter value",
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn drop_realm_destroys_children() {
        with_sandbox("drop_realm_destroys_children", |sandbox| async move {
            let realm = create_test_realm(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![COUNTER_SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    }]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            let counter = connect_to_service::<CounterMarker>(&realm);
            assert_eq!(
                counter
                    .increment()
                    .await
                    .expect("fuchsia.netemul.test/Counter.increment call failed"),
                1,
                "unexpected counter value",
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
                    create_test_realm(
                        &sandbox,
                        fnetemul::RealmOptions {
                            children: Some(vec![fnetemul::ChildDef {
                                url: Some(COUNTER_PACKAGE_URL.to_string()),
                                name: Some(COUNTER_COMPONENT_NAME.to_string()),
                                exposes: Some(vec![COUNTER_SERVICE_NAME.to_string()]),
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
                let counter = connect_to_service::<CounterMarker>(&realm);
                assert_eq!(
                    counter
                        .increment()
                        .await
                        .expect("fuchsia.netemul.test/Counter.increment call failed"),
                    1,
                    "unexpected counter value",
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
            let realm = create_test_realm(
                &sandbox,
                fnetemul::RealmOptions {
                    name: Some("test-realm-name".to_string()),
                    children: Some(vec![fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        exposes: Some(vec![COUNTER_SERVICE_NAME.to_string()]),
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
                format!(
                    "{}\\:set_realm_name0-test-realm-name",
                    fuchsia_component_test::DEFAULT_COLLECTION_NAME,
                ),
            );
        })
        .await
    }

    #[fasync::run_singlethreaded(test)]
    async fn auto_generated_realm_name() {
        with_sandbox("auto_generated_realm_name", |sandbox| async move {
            const REALMS_COUNT: usize = 10;
            for i in 0..REALMS_COUNT {
                let realm = create_test_realm(
                    &sandbox,
                    fnetemul::RealmOptions {
                        name: None,
                        children: Some(vec![fnetemul::ChildDef {
                            url: Some(COUNTER_PACKAGE_URL.to_string()),
                            name: Some(COUNTER_COMPONENT_NAME.to_string()),
                            exposes: Some(vec![COUNTER_SERVICE_NAME.to_string()]),
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
                    format!(
                        "{}\\:auto_generated_realm_name{}-realm",
                        fuchsia_component_test::DEFAULT_COLLECTION_NAME,
                        i,
                    ),
                );
            }
        })
        .await
    }
}
