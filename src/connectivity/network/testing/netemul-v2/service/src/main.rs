// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fidl::endpoints::{DiscoverableService, ServerEnd},
    fidl_fuchsia_io as fio, fidl_fuchsia_io2 as fio2, fidl_fuchsia_logger as flogger,
    fidl_fuchsia_net_tun as fnet_tun,
    fidl_fuchsia_netemul::{
        self as fnetemul, ChildDef, ChildUses, ManagedRealmMarker, ManagedRealmRequest,
        RealmOptions, SandboxRequest, SandboxRequestStream,
    },
    fidl_fuchsia_netemul_network as fnetemul_network, fidl_fuchsia_process as fprocess,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    fuchsia_component_test::{
        self as fcomponent,
        builder::{Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint},
    },
    fuchsia_zircon as zx,
    futures::{channel::mpsc, FutureExt as _, SinkExt as _, StreamExt as _, TryStreamExt as _},
    log::{debug, error, info, warn},
    pin_utils::pin_mut,
    std::collections::{HashMap, HashSet},
    std::sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
    thiserror::Error,
    vfs::directory::{
        entry::DirectoryEntry as _, helper::DirectlyMutable as _,
        mutable::simple::Simple as SimpleMutableDir,
    },
};

type Result<T = (), E = anyhow::Error> = std::result::Result<T, E>;

const REALM_COLLECTION_NAME: &str = "netemul";
const NETEMUL_SERVICES_COMPONENT_NAME: &str = "netemul-services";
const DEVFS: &str = "dev";
const DEVFS_PATH: &str = "/dev";

#[derive(Error, Debug)]
enum CreateRealmError {
    #[error("url not provided")]
    UrlNotProvided,
    #[error("name not provided")]
    NameNotProvided,
    #[error("capability source not provided")]
    CapabilitySourceNotProvided,
    #[error("capability name not provided")]
    CapabilityNameNotProvided,
    #[error("duplicate capability '{0}' used by component '{1}'")]
    DuplicateCapabilityUse(String, String),
    #[error("realm builder error: {0:?}")]
    RealmBuilderError(#[from] fcomponent::error::Error),
}

impl Into<zx::Status> for CreateRealmError {
    fn into(self) -> zx::Status {
        match self {
            CreateRealmError::UrlNotProvided
            | CreateRealmError::NameNotProvided
            | CreateRealmError::CapabilitySourceNotProvided
            | CreateRealmError::CapabilityNameNotProvided
            | CreateRealmError::DuplicateCapabilityUse(_, _)
            | CreateRealmError::RealmBuilderError(fcomponent::error::Error::Builder(
                fcomponent::error::BuilderError::MissingRouteSource(_),
            )) => zx::Status::INVALID_ARGS,
            CreateRealmError::RealmBuilderError(_) => zx::Status::INTERNAL,
        }
    }
}

async fn create_realm_instance(
    RealmOptions { name, children, .. }: RealmOptions,
    prefix: &str,
    network_realm: Arc<fcomponent::RealmInstance>,
    devfs: fio::DirectoryProxy,
) -> Result<fcomponent::RealmInstance, CreateRealmError> {
    // Keep track of all the services exposed by components in the test realm, as well as components
    // requesting that all available capabilities be routed to them, so that we can wait until we've
    // seen all the child component definitions to route those capabilities.
    let mut exposed_services = HashMap::new();
    let mut components_using_all = Vec::new();
    // Keep track of dependencies between child components in the test realm in order to create the
    // relevant routes at the end. RealmBuilder doesn't allow creating routes between components if
    // the components haven't both been created yet, so we wait until all components have been
    // created to add routes between them.
    let mut child_dep_routes = Vec::new();
    let mut builder = RealmBuilder::new().await?;
    let _: &mut RealmBuilder = builder
        .add_component(
            NETEMUL_SERVICES_COMPONENT_NAME,
            ComponentSource::Mock(fcomponent::mock::Mock::new(
                move |mock_handles: fcomponent::mock::MockHandles| {
                    Box::pin(run_netemul_services(
                        mock_handles,
                        network_realm.clone(),
                        Clone::clone(&devfs),
                    ))
                },
            )),
        )
        .await?;
    for ChildDef { url, name, exposes, uses, .. } in children.unwrap_or_default() {
        let url = url.ok_or(CreateRealmError::UrlNotProvided)?;
        let name = name.ok_or(CreateRealmError::NameNotProvided)?;
        let _: &mut RealmBuilder =
            builder.add_component(name.as_ref(), ComponentSource::url(&url)).await?;
        if let Some(exposes) = exposes {
            for exposed in exposes {
                // TODO(https://fxbug.dev/72043): allow duplicate services.
                // Service names will be aliased as `child_name/service_name`, and this panic will
                // be replaced with an INVALID_ARGS epitaph sent on the `ManagedRealm` channel if a
                // child component with a duplicate name is created, or if a child exposes two
                // services of the same name.
                match exposed_services.entry(exposed) {
                    std::collections::hash_map::Entry::Occupied(entry) => {
                        panic!(
                            "duplicate service name '{}' exposed from component '{}'",
                            entry.key(),
                            entry.get(),
                        );
                    }
                    std::collections::hash_map::Entry::Vacant(entry) => {
                        let _: &mut RealmBuilder = builder.add_route(CapabilityRoute {
                            capability: Capability::protocol(entry.key()),
                            source: RouteEndpoint::component(&name),
                            targets: vec![RouteEndpoint::AboveRoot],
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
                    // TODO(https://fxbug.dev/76380): route netemul-provided `/dev`.
                    // TODO(https://fxbug.dev/72403): route netemul-provided `SyncManager`.
                    let () = route_log_sink_to_component(&mut builder, &name)?;
                    let () = route_network_context_to_component(&mut builder, &name)?;
                    let () = components_using_all.push(name);
                }
                ChildUses::Capabilities(caps) => {
                    let mut unique_caps = HashSet::new();
                    for cap in caps {
                        let service_name = match cap {
                            fnetemul::Capability::LogSink(fnetemul::Empty {}) => {
                                let () = route_log_sink_to_component(&mut builder, &name)?;
                                flogger::LogSinkMarker::SERVICE_NAME.to_string()
                            }
                            fnetemul::Capability::NetemulNetworkContext(fnetemul::Empty {}) => {
                                let () = route_network_context_to_component(&mut builder, &name)?;
                                fnetemul_network::NetworkContextMarker::SERVICE_NAME.to_string()
                            }
                            fnetemul::Capability::NetemulSyncManager(fnetemul::Empty {}) => todo!(),
                            fnetemul::Capability::NetemulDevfs(fnetemul::Empty {}) => todo!(),
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                name: source,
                                capability,
                                ..
                            }) => {
                                let source =
                                    source.ok_or(CreateRealmError::CapabilitySourceNotProvided)?;
                                let fnetemul::ExposedCapability::Service(capability) =
                                    capability
                                        .ok_or(CreateRealmError::CapabilityNameNotProvided)?;
                                debug!(
                                    "routing capability '{}' from component '{}' to '{}'",
                                    capability, source, name
                                );
                                let () = child_dep_routes.push(CapabilityRoute {
                                    capability: Capability::protocol(&capability),
                                    source: RouteEndpoint::component(source),
                                    targets: vec![RouteEndpoint::component(&name)],
                                });
                                capability
                            }
                        };
                        if !unique_caps.insert(service_name.clone()) {
                            return Err(CreateRealmError::DuplicateCapabilityUse(
                                service_name,
                                name,
                            ));
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
            let _: &mut RealmBuilder = builder.add_route(CapabilityRoute {
                capability: Capability::protocol(service),
                source: RouteEndpoint::component(source),
                targets: vec![RouteEndpoint::component(&component)],
            })?;
        }
    }
    for route in child_dep_routes {
        let _: &mut RealmBuilder = builder.add_route(route)?;
    }
    let mut realm = builder.build();
    // Mark all dependencies between components in the test realm as weak, to allow for dependency
    // cycles.
    //
    // TODO(https://fxbug.dev/74977): once we can specify weak dependencies directly with the
    // RealmBuilder API, only mark dependencies as `weak` that originated from a `ChildUses.all`
    // configuration.
    let cm_rust::ComponentDecl { offers, .. } = realm.get_decl_mut(&fcomponent::Moniker::root())?;
    for offer in offers {
        match offer {
            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                dependency_type,
                source,
                ..
            }) => {
                // No need to mark dependencies on the built-in netemul services component as weak,
                // since it doesn't depend on any services exposed by other components in the test
                // realm.
                match source {
                    cm_rust::OfferSource::Child(name)
                        if name == NETEMUL_SERVICES_COMPONENT_NAME =>
                    {
                        continue;
                    }
                    _ => (),
                }
                *dependency_type = cm_rust::DependencyType::WeakForMigration;
            }
            offer => {
                error!(
                    "there should only be protocol offers from the root of the managed realm; \
                    found {:?}",
                    offer
                );
            }
        }
    }
    let name =
        name.map(|name| format!("{}-{}", prefix, name)).unwrap_or_else(|| prefix.to_string());
    info!("creating new ManagedRealm with name '{}'", name);
    let () = realm.set_collection_name(REALM_COLLECTION_NAME);
    realm.create_with_name(name).await.map_err(Into::into)
}

struct ManagedRealm {
    server_end: ServerEnd<ManagedRealmMarker>,
    realm: fcomponent::RealmInstance,
    devfs: Arc<SimpleMutableDir>,
}

impl ManagedRealm {
    async fn run_service(self) -> Result {
        let Self { server_end, realm, devfs } = self;
        let mut stream = server_end.into_stream().context("failed to acquire request stream")?;
        while let Some(request) = stream.try_next().await.context("FIDL error")? {
            match request {
                ManagedRealmRequest::GetMoniker { responder } => {
                    let moniker =
                        format!("{}\\:{}", REALM_COLLECTION_NAME, realm.root.child_name());
                    let () =
                        responder.send(&moniker).context("responding to GetMoniker request")?;
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
                ManagedRealmRequest::GetDevfs { devfs: server_end, control_handle: _ } => {
                    let () = devfs.clone().open(
                        vfs::execution_scope::ExecutionScope::new(),
                        fio::OPEN_RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        vfs::path::Path::empty(),
                        server_end.into_channel().into(),
                    );
                }
                ManagedRealmRequest::AddDevice { path, device, responder } => {
                    // ClientEnd::into_proxy should only return an Err when there is no executor, so
                    // this is not expected to ever cause a panic.
                    let device = device.into_proxy().expect("failed to get device proxy");
                    let path_clone = path.clone();
                    // TODO(https://fxbug.dev/76468): if `path` contains separators, for example
                    // "class/ethernet/test_device", create the necessary intermediary directories
                    // accordingly.
                    let response = devfs.add_entry(
                        &path,
                        vfs::service::endpoint(
                            move |_: vfs::execution_scope::ExecutionScope, channel| {
                                let () = device
                                    .clone()
                                    .serve_device(channel.into_zx_channel())
                                    .unwrap_or_else(|e| {
                                        error!(
                                            "failed to serve device on path {}: {:?}",
                                            path_clone, e
                                        )
                                    });
                            },
                        ),
                    );
                    match response {
                        Ok(()) => info!("adding virtual device at path '{}/{}'", DEVFS_PATH, path),
                        Err(e) => {
                            if e == zx::Status::ALREADY_EXISTS {
                                warn!(
                                    "cannot add device at path '{}/{}': path is already in use",
                                    DEVFS_PATH, path
                                )
                            } else {
                                error!(
                                    "unexpected error adding entry at path '{}/{}': {}",
                                    DEVFS_PATH, path, e
                                )
                            }
                        }
                    }
                    let () = responder
                        .send(&mut response.map_err(zx::Status::into_raw))
                        .context("responding to AddDevice request")?;
                }
                ManagedRealmRequest::RemoveDevice { path, responder } => {
                    let response = match devfs.remove_entry(&path, false) {
                        Ok(entry) => {
                            if let Some(entry) = entry {
                                let _: Arc<dyn vfs::directory::entry::DirectoryEntry> = entry;
                                info!("removing virtual device at path '{}/{}'", DEVFS_PATH, path);
                                Ok(())
                            } else {
                                warn!(
                                    "cannot remove device at path '{}/{}': path is not currently \
                                    bound to a device",
                                    DEVFS_PATH, path,
                                );
                                Err(zx::Status::NOT_FOUND)
                            }
                        }
                        Err(e) => {
                            error!(
                                "error removing device at path '{}/{}': {}",
                                DEVFS_PATH, path, e
                            );
                            Err(e)
                        }
                    };
                    let () = responder
                        .send(&mut response.map_err(zx::Status::into_raw))
                        .context("responding to RemoveDevice request")?;
                }
            }
        }
        Ok(())
    }
}

fn route_log_sink_to_component(
    builder: &mut RealmBuilder,
    component: &str,
) -> Result<(), fcomponent::error::Error> {
    let _: &mut RealmBuilder = builder.add_route(CapabilityRoute {
        capability: Capability::protocol(flogger::LogSinkMarker::SERVICE_NAME),
        source: RouteEndpoint::AboveRoot,
        targets: vec![RouteEndpoint::component(component)],
    })?;
    Ok(())
}

fn route_network_context_to_component(
    builder: &mut RealmBuilder,
    component: &str,
) -> Result<(), fcomponent::error::Error> {
    let _: &mut RealmBuilder = builder.add_route(CapabilityRoute {
        capability: Capability::protocol(fnetemul_network::NetworkContextMarker::SERVICE_NAME),
        source: RouteEndpoint::component(NETEMUL_SERVICES_COMPONENT_NAME),
        targets: vec![RouteEndpoint::component(component)],
    })?;
    Ok(())
}

async fn run_netemul_services(
    mock_handles: fcomponent::mock::MockHandles,
    network_realm: impl std::ops::Deref<Target = fcomponent::RealmInstance> + 'static,
    devfs: fio::DirectoryProxy,
) -> Result {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> = fs
        .dir("svc")
        .add_service_at(fnetemul_network::NetworkContextMarker::SERVICE_NAME, |channel| {
            Some(ServerEnd::<fnetemul_network::NetworkContextMarker>::new(channel))
        });
    let () = fs.add_remote(DEVFS, devfs);
    let _: &mut ServiceFs<_> = fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    let () = fs
        .for_each_concurrent(None, |server_end| {
            futures::future::ready(
                network_realm
                    .root
                    .connect_request_to_protocol_at_exposed_dir(server_end)
                    .unwrap_or_else(|e| error!("failed to open protocol in directory: {:?}", e)),
            )
        })
        .await;
    Ok(())
}

fn make_devfs() -> Result<(fio::DirectoryProxy, Arc<SimpleMutableDir>)> {
    let (proxy, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .context("create directory marker")?;
    let dir = vfs::directory::mutable::simple::simple();
    let () = dir.clone().open(
        vfs::execution_scope::ExecutionScope::new(),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        fio::MODE_TYPE_DIRECTORY,
        vfs::path::Path::empty(),
        server.into_channel().into(),
    );
    Ok((proxy, dir))
}

const NETWORK_CONTEXT_COMPONENT_NAME: &str = "network-context";
const ISOLATED_DEVMGR_COMPONENT_NAME: &str = "isolated-devmgr";
const NETWORK_TUN_COMPONENT_NAME: &str = "network-tun";

#[derive(serde::Deserialize)]
struct Package {
    name: String,
}

// TODO(https://fxbug.dev/67854): remove once we have relative package URLs.
const PACKAGE_IDENTITY_FILE: &str = "/pkg/meta/package";

async fn get_this_package_name() -> Result<String> {
    let contents = io_util::file::read_in_namespace_to_string(PACKAGE_IDENTITY_FILE)
        .await
        .context("error opening file")?;
    let Package { name } =
        serde_json::from_str(&contents).context("failed to deserialize package identity file")?;
    Ok(name)
}

async fn setup_network_realm(
    sandbox_name: impl std::fmt::Display,
) -> Result<fcomponent::RealmInstance> {
    let pkg = get_this_package_name().await?;
    let package_url = |component_name: &str| {
        format!("fuchsia-pkg://fuchsia.com/{}#meta/{}.cm", pkg, component_name)
    };
    let network_context_package_url = package_url(NETWORK_CONTEXT_COMPONENT_NAME);
    let isolated_devmgr_package_url = package_url(ISOLATED_DEVMGR_COMPONENT_NAME);
    let network_tun_package_url = package_url(NETWORK_TUN_COMPONENT_NAME);

    let mut builder = RealmBuilder::new().await.context("error creating new realm builder")?;
    let _: &mut RealmBuilder = builder
        .add_component(
            NETWORK_CONTEXT_COMPONENT_NAME,
            ComponentSource::url(network_context_package_url),
        )
        .await
        .context("error adding network-context component")?
        .add_component(
            ISOLATED_DEVMGR_COMPONENT_NAME,
            ComponentSource::url(isolated_devmgr_package_url),
        )
        .await
        .context("error adding isolated-devmgr component")?
        .add_component(NETWORK_TUN_COMPONENT_NAME, ComponentSource::url(network_tun_package_url))
        .await
        .context("error adding network-tun component")?
        .add_route(CapabilityRoute {
            capability: Capability::protocol(fnetemul_network::NetworkContextMarker::SERVICE_NAME),
            source: RouteEndpoint::component(NETWORK_CONTEXT_COMPONENT_NAME),
            targets: vec![RouteEndpoint::AboveRoot],
        })
        .with_context(|| {
            format!(
                "error adding route exposing capability '{}' from component '{}'",
                fnetemul_network::NetworkContextMarker::SERVICE_NAME,
                NETWORK_CONTEXT_COMPONENT_NAME
            )
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory(DEVFS, DEVFS_PATH, fio2::R_STAR_DIR),
            source: RouteEndpoint::component(ISOLATED_DEVMGR_COMPONENT_NAME),
            targets: vec![RouteEndpoint::component(NETWORK_CONTEXT_COMPONENT_NAME)],
        })
        .with_context(|| {
            format!(
                "error adding route offering directory 'dev' from component '{}' to '{}'",
                ISOLATED_DEVMGR_COMPONENT_NAME, NETWORK_CONTEXT_COMPONENT_NAME
            )
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol(fnet_tun::ControlMarker::SERVICE_NAME),
            source: RouteEndpoint::component(NETWORK_TUN_COMPONENT_NAME),
            targets: vec![RouteEndpoint::component(NETWORK_CONTEXT_COMPONENT_NAME)],
        })
        .with_context(|| {
            format!(
                "error adding route offering capability '{}' from component '{}'",
                fnet_tun::ControlMarker::SERVICE_NAME,
                NETWORK_CONTEXT_COMPONENT_NAME
            )
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol(fprocess::LauncherMarker::SERVICE_NAME),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(ISOLATED_DEVMGR_COMPONENT_NAME)],
        })
        .with_context(|| {
            format!(
                "error adding route offering capability '{}' to components",
                fprocess::LauncherMarker::SERVICE_NAME,
            )
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol(flogger::LogSinkMarker::SERVICE_NAME),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component(NETWORK_CONTEXT_COMPONENT_NAME),
                RouteEndpoint::component(ISOLATED_DEVMGR_COMPONENT_NAME),
                RouteEndpoint::component(NETWORK_TUN_COMPONENT_NAME),
            ],
        })
        .with_context(|| {
            format!(
                "error adding route offering capability '{}' to components",
                flogger::LogSinkMarker::SERVICE_NAME,
            )
        })?;
    let mut realm = builder.build();
    let () = realm.set_collection_name(REALM_COLLECTION_NAME);
    realm
        .create_with_name(format!("{}-network-realm", sandbox_name))
        .await
        .context("error creating realm instance")
}

async fn handle_sandbox(
    stream: SandboxRequestStream,
    sandbox_name: impl std::fmt::Display,
) -> Result {
    let (tx, rx) = mpsc::channel(1);
    let realm_index = AtomicU64::new(0);
    // TODO(https://fxbug.dev/74534): define only one instance of `network-context` and associated
    // components, and do routing statically, once we no longer need `isolated-devmgr`.
    let network_realm = Arc::new(
        setup_network_realm(&sandbox_name).await.context("failed to setup network realm")?,
    );
    let sandbox_fut = stream.err_into::<anyhow::Error>().try_for_each_concurrent(None, |request| {
        let mut tx = tx.clone();
        let sandbox_name = &sandbox_name;
        let realm_index = &realm_index;
        let network_realm = &network_realm;
        async move {
            match request {
                SandboxRequest::CreateRealm { realm: server_end, options, control_handle: _ } => {
                    let index = realm_index.fetch_add(1, Ordering::SeqCst);
                    let prefix = format!("{}{}", sandbox_name, index);
                    let (proxy, devfs) = make_devfs().context("creating devfs")?;
                    match create_realm_instance(options, &prefix, network_realm.clone(), proxy)
                        .await
                    {
                        Ok(realm) => tx
                            .send(ManagedRealm { server_end, realm, devfs })
                            .await
                            .expect("receiver should not be closed"),
                        Err(e) => {
                            error!("error creating ManagedRealm: {}", e);
                            server_end
                                .close_with_epitaph(e.into())
                                .unwrap_or_else(|e| error!("error sending epitaph: {:?}", e))
                        }
                    }
                }
                SandboxRequest::GetNetworkContext { network_context, control_handle: _ } => {
                    network_realm
                        .root
                        .connect_request_to_protocol_at_exposed_dir(network_context)
                        .unwrap_or_else(|e| error!("error getting NetworkContext: {:?}", e))
                }
                SandboxRequest::GetSyncManager { sync_manager: _, control_handle: _ } => {
                    todo!("https://fxbug.dev/72403): route netemul-provided sync manager")
                }
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
        result = sandbox_fut => Ok(result?),
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
        fidl::endpoints::Proxy as _, fidl_fuchsia_device as fdevice,
        fidl_fuchsia_netemul as fnetemul, fidl_fuchsia_netemul_test::CounterMarker,
        fixture::fixture, fuchsia_vfs_watcher as fvfs_watcher, std::convert::TryFrom as _,
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

    async fn with_sandbox<F, Fut>(name: &str, test: F)
    where
        F: FnOnce(fnetemul::SandboxProxy) -> Fut,
        Fut: futures::Future<Output = ()>,
    {
        let (sandbox, fut) = setup_sandbox_service(name);
        let ((), ()) = futures::future::join(fut, test(sandbox)).await;
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

    const TEST_DRIVER_COMPONENT_NAME: &str = "test_driver";
    const COUNTER_COMPONENT_NAME: &str = "counter";
    const COUNTER_PACKAGE_URL: &str = "fuchsia-pkg://fuchsia.com/netemul-v2-tests#meta/counter.cm";

    fn counter_component() -> fnetemul::ChildDef {
        fnetemul::ChildDef {
            url: Some(COUNTER_PACKAGE_URL.to_string()),
            name: Some(COUNTER_COMPONENT_NAME.to_string()),
            exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
            uses: Some(fnetemul::ChildUses::Capabilities(vec![fnetemul::Capability::LogSink(
                fnetemul::Empty {},
            )])),
            ..fnetemul::ChildDef::EMPTY
        }
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn can_connect_to_single_service(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_service::<CounterMarker>();
        assert_eq!(
            counter.increment().await.expect("fuchsia.netemul.test/Counter.increment call failed"),
            1,
        );
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn multiple_realms(sandbox: fnetemul::SandboxProxy) {
        let realm_a = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                name: Some("a".to_string()),
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let realm_b = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                name: Some("b".to_string()),
                children: Some(vec![counter_component()]),
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
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn drop_realm_destroys_children(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_service::<CounterMarker>();
        assert_eq!(
            counter.increment().await.expect("fuchsia.netemul.test/Counter.increment call failed"),
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
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn drop_sandbox_destroys_realms(sandbox: fnetemul::SandboxProxy) {
        const REALMS_COUNT: usize = 10;
        let realms = std::iter::repeat(())
            .take(REALMS_COUNT)
            .map(|()| {
                TestRealm::new(
                    &sandbox,
                    fnetemul::RealmOptions {
                        children: Some(vec![counter_component()]),
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
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn set_realm_name(sandbox: fnetemul::SandboxProxy) {
        let TestRealm { realm } = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                name: Some("test-realm-name".to_string()),
                children: Some(vec![counter_component()]),
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
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn auto_generated_realm_name(sandbox: fnetemul::SandboxProxy) {
        const REALMS_COUNT: usize = 10;
        for i in 0..REALMS_COUNT {
            let TestRealm { realm } = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    name: None,
                    children: Some(vec![counter_component()]),
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
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn inspect(sandbox: fnetemul::SandboxProxy) {
        const REALMS_COUNT: usize = 10;
        let realms = std::iter::repeat(())
            .take(REALMS_COUNT)
            .map(|()| {
                TestRealm::new(
                    &sandbox,
                    fnetemul::RealmOptions {
                        children: Some(vec![counter_component()]),
                        ..fnetemul::RealmOptions::EMPTY
                    },
                )
            })
            // Collect the `TestRealm`s because we want all the test realms to be alive for the
            // duration of the test.
            //
            // Each `TestRealm` owns a `ManagedRealmProxy`, which has RAII semantics: when the proxy
            // is dropped, the backing test realm managed by the sandbox is also destroyed.
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
                        diagnostics_reader::assert_data_tree!(data, root: {
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
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn child_uses_all_capabilities(sandbox: fnetemul::SandboxProxy) {
        // These services are aliased instances of the `fuchsia.netemul.test.Counter` service
        // (configured in the component manifest), so there is no actual `CounterAMarker` type, for
        // example, from which we could extract its `SERVICE_NAME`.
        //
        // TODO(https://fxbug.dev/72043): once we allow duplicate service names, verify that we can
        // also disambiguate services by specifying the component moniker of the component exposing
        // the service, in addition to the A/B aliasing used here.
        const COUNTER_A_SERVICE_NAME: &str = "fuchsia.netemul.test.CounterA";
        const COUNTER_B_SERVICE_NAME: &str = "fuchsia.netemul.test.CounterB";

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
                    // TODO(https://fxbug.dev/74868): once we can allow the ERROR logs that result
                    // from the routing failure, add a child that does *not* use `All`, and verify
                    // that it does not have access to the other components' exposed services.
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
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn child_uses_all_netemul_services(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![fnetemul::ChildDef {
                    url: Some(COUNTER_PACKAGE_URL.to_string()),
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                    uses: Some(fnetemul::ChildUses::All(fnetemul::Empty {})),
                    ..fnetemul::ChildDef::EMPTY
                }]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_service::<CounterMarker>();
        let (network_context, server_end) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkContextMarker>()
                .expect("failed to create network context proxy");
        let () = counter
            .connect_to_service(
                fnetemul_network::NetworkContextMarker::SERVICE_NAME,
                server_end.into_channel(),
            )
            .expect("failed to connect to network context through counter");
        matches::assert_matches!(
            network_context.setup(&mut Vec::new().iter_mut()).await,
            Ok((zx::sys::ZX_OK, Some(_setup_handle)))
        );
        // TODO(https://fxbug.dev/76380): ensure that `counter` has access to `/dev` through
        // Capability::NetemulDevfs.
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn network_context(sandbox: fnetemul::SandboxProxy) {
        let (network_ctx, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkContextMarker>()
                .expect("failed to create network context proxy");
        let () = sandbox.get_network_context(server).expect("calling get network context");
        let (endpoint_mgr, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::EndpointManagerMarker>()
                .expect("failed to create endpoint manager proxy");
        let () = network_ctx.get_endpoint_manager(server).expect("calling get endpoint manager");
        let endpoints = endpoint_mgr.list_endpoints().await.expect("calling list endpoints");
        assert_eq!(endpoints, Vec::<String>::new());

        let backings = [
            fnetemul_network::EndpointBacking::Ethertap,
            fnetemul_network::EndpointBacking::NetworkDevice,
        ];
        for (i, backing) in backings.iter().enumerate() {
            let name = format!("ep{}", i);
            let (status, endpoint) = endpoint_mgr
                .create_endpoint(
                    &name,
                    &mut fnetemul_network::EndpointConfig {
                        mtu: 1500,
                        mac: None,
                        backing: *backing,
                    },
                )
                .await
                .expect("calling create endpoint");
            let () = zx::Status::ok(status).expect("endpoint creation");
            let endpoint = endpoint
                .expect("endpoint creation")
                .into_proxy()
                .expect("failed to create endpoint proxy");
            assert_eq!(endpoint.get_name().await.expect("calling get name"), name);
            assert_eq!(
                endpoint.get_config().await.expect("calling get config"),
                fnetemul_network::EndpointConfig { mtu: 1500, mac: None, backing: *backing }
            );
        }
    }

    fn get_network_manager(
        sandbox: &fnetemul::SandboxProxy,
    ) -> fnetemul_network::NetworkManagerProxy {
        let (network_ctx, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkContextMarker>()
                .expect("failed to create network context proxy");
        let () = sandbox.get_network_context(server).expect("calling get network context");
        let (network_mgr, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkManagerMarker>()
                .expect("failed to create network manager proxy");
        let () = network_ctx.get_network_manager(server).expect("calling get network manager");
        network_mgr
    }

    #[fuchsia::test]
    async fn network_context_per_sandbox_connection() {
        let (sandbox1, sandbox1_fut) = setup_sandbox_service("sandbox_1");
        let (sandbox2, sandbox2_fut) = setup_sandbox_service("sandbox_2");
        let test = async move {
            let net_mgr1 = get_network_manager(&sandbox1);
            let net_mgr2 = get_network_manager(&sandbox2);

            let (status, _network) = net_mgr1
                .create_network("network", fnetemul_network::NetworkConfig::EMPTY)
                .await
                .expect("calling create network");
            let () = zx::Status::ok(status).expect("network creation");
            let (status, _network) = net_mgr1
                .create_network("network", fnetemul_network::NetworkConfig::EMPTY)
                .await
                .expect("calling create network");
            assert_eq!(zx::Status::from_raw(status), zx::Status::ALREADY_EXISTS);

            let (status, _network) = net_mgr2
                .create_network("network", fnetemul_network::NetworkConfig::EMPTY)
                .await
                .expect("calling create network");
            let () = zx::Status::ok(status).expect("network creation");
            drop(sandbox1);
            drop(sandbox2);
        };
        let ((), (), ()) = futures::future::join3(
            sandbox1_fut.map(|()| info!("sandbox1_fut complete")),
            sandbox2_fut.map(|()| info!("sandbox2_fut complete")),
            test.map(|()| info!("test complete")),
        )
        .await;
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn network_context_used_by_child(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![
                    fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some("counter-with-network-context".to_string()),
                        exposes: Some(vec![CounterMarker::SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                            fnetemul::Capability::NetemulNetworkContext(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    // TODO(https://fxbug.dev/74868): when we can allow ERROR logs for routing
                    // errors, add a child component that does not `use` NetworkContext, and verify
                    // that we cannot get at NetworkContext through it. It should result in a
                    // zx::Status::UNAVAILABLE error.
                ]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_service::<CounterMarker>();
        let (network_context, server_end) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkContextMarker>()
                .expect("failed to create network context proxy");
        let () = counter
            .connect_to_service(
                fnetemul_network::NetworkContextMarker::SERVICE_NAME,
                server_end.into_channel(),
            )
            .expect("failed to connect to network context through counter");
        matches::assert_matches!(
            network_context.setup(&mut Vec::new().iter_mut()).await,
            Ok((zx::sys::ZX_OK, Some(_setup_handle)))
        );
    }

    #[fixture(with_sandbox)]
    // TODO(https://fxbug.dev/74868): when we can allowlist particular ERROR logs in a test, we can
    // use #[fuchsia::test] which initializes syslog.
    #[fasync::run_singlethreaded(test)]
    async fn create_realm_invalid_options(sandbox: fnetemul::SandboxProxy) {
        // TODO(https://github.com/frondeus/test-case/issues/37): consider using the #[test_case]
        // macro to define these cases statically, if we can access the name of the test case from
        // the test case body. This is necessary in order to avoid creating sandboxes with colliding
        // names at runtime.
        //
        // Note, however, that rustfmt struggles with macros, and using test-case for this test
        // would result in a lot of large struct literals defined as macro arguments of
        // #[test_case]. This may be more readable as an auto-formatted array.
        //
        // TODO(https://fxbug.dev/76384): refactor how we specify the test cases to make it easier
        // to tell why a given case is invalid.
        struct TestCase<'a> {
            name: &'a str,
            children: Vec<fnetemul::ChildDef>,
            epitaph: zx::Status,
        }
        let cases = [
            TestCase {
                name: "no url provided",
                children: vec![fnetemul::ChildDef {
                    url: None,
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "no name provided",
                children: vec![fnetemul::ChildDef {
                    url: Some(COUNTER_PACKAGE_URL.to_string()),
                    name: None,
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "name not specified for child dependency",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    url: Some(COUNTER_PACKAGE_URL.to_string()),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                            name: None,
                            capability: Some(fnetemul::ExposedCapability::Service(
                                CounterMarker::SERVICE_NAME.to_string(),
                            )),
                            ..fnetemul::ChildDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "capability not specified for child dependency",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    url: Some(COUNTER_PACKAGE_URL.to_string()),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                            name: Some("component".to_string()),
                            capability: None,
                            ..fnetemul::ChildDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "duplicate service used by child",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    url: Some(COUNTER_PACKAGE_URL.to_string()),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "child manually depends on a duplicate of a netemul-provided service",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    url: Some(COUNTER_PACKAGE_URL.to_string()),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                            name: Some("root".to_string()),
                            capability: Some(fnetemul::ExposedCapability::Service(
                                flogger::LogSinkMarker::SERVICE_NAME.to_string(),
                            )),
                            ..fnetemul::ChildDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "child depends on nonexistent child",
                children: vec![
                    counter_component(),
                    fnetemul::ChildDef {
                        name: Some("counter-b".to_string()),
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                // counter-a does not exist.
                                name: Some("counter-a".to_string()),
                                capability: Some(fnetemul::ExposedCapability::Service(
                                    CounterMarker::SERVICE_NAME.to_string(),
                                )),
                                ..fnetemul::ChildDep::EMPTY
                            }),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                ],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "duplicate components",
                children: vec![
                    fnetemul::ChildDef {
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    fnetemul::ChildDef {
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        ..fnetemul::ChildDef::EMPTY
                    },
                ],
                epitaph: zx::Status::INTERNAL,
            },
            // TODO(https://fxbug.dev/72043): once we allow duplicate services, verify that a child
            // exposing duplicate services results in a ZX_ERR_INTERNAL epitaph.
            //
            // TODO(https://fxbug.dev/74977): once we only mark dependencies as `weak` that
            // originated from a `ChildUses.all` configuration, verify that an explicit dependency
            // cycle between components in a test realm (not using `ChildUses.all`) results in a
            // ZX_ERR_INTERNAL epitaph.
        ];
        for TestCase { name, children, epitaph } in std::array::IntoIter::new(cases) {
            let TestRealm { realm } = TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(children),
                    ..fnetemul::RealmOptions::EMPTY
                },
            );
            match realm.take_event_stream().next().await.expect(&format!(
                "test case failed: \"{}\": epitaph should be sent on realm channel",
                name
            )) {
                Err(fidl::Error::ClientChannelClosed {
                    status,
                    service_name: <ManagedRealmMarker as fidl::endpoints::ServiceMarker>::DEBUG_NAME,
                }) if status == epitaph => (),
                event => panic!(
                    "test case failed: \"{}\": expected channel close with epitaph {}, got \
                        unexpected event on realm channel: {:?}",
                    name, epitaph, event
                ),
            }
        }
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn child_dep(sandbox: fnetemul::SandboxProxy) {
        const COUNTER_A_SERVICE_NAME: &str = "fuchsia.netemul.test.CounterA";
        const COUNTER_B_SERVICE_NAME: &str = "fuchsia.netemul.test.CounterB";
        let TestRealm { realm } = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![
                    fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some("counter-a".to_string()),
                        exposes: Some(vec![COUNTER_A_SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                name: Some("counter-b".to_string()),
                                capability: Some(fnetemul::ExposedCapability::Service(
                                    COUNTER_B_SERVICE_NAME.to_string(),
                                )),
                                ..fnetemul::ChildDep::EMPTY
                            }),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    fnetemul::ChildDef {
                        url: Some(COUNTER_PACKAGE_URL.to_string()),
                        name: Some("counter-b".to_string()),
                        exposes: Some(vec![COUNTER_B_SERVICE_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                ]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter_a = {
            let (counter_a, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
                .expect("failed to create CounterA proxy");
            let () = realm
                .connect_to_service(COUNTER_A_SERVICE_NAME, None, server_end.into_channel())
                .expect("failed to connect to CounterA service");
            counter_a
        };
        // counter-a should have access to counter-b's exposed service.
        let (counter_b, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
            .expect("failed to create CounterB proxy");
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
        // The counter-b service that counter-a has access to should be the same one accessible
        // through the test realm.
        let counter_b = {
            let (counter_b, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
                .expect("failed to create CounterB proxy");
            let () = realm
                .connect_to_service(COUNTER_B_SERVICE_NAME, None, server_end.into_channel())
                .expect("failed to connect to CounterB service");
            counter_b
        };
        assert_eq!(
            counter_b
                .increment()
                .await
                .expect("fuchsia.netemul.test/CounterB.increment call failed"),
            2,
        );
        // TODO(https://fxbug.dev/74868): once we can allow the ERROR logs that result from the
        // routing failure, verify that counter-b does *not* have access to counter-a's service.
    }

    async fn create_endpoint(
        sandbox: &fnetemul::SandboxProxy,
        name: &str,
        mut config: fnetemul_network::EndpointConfig,
    ) -> fnetemul_network::EndpointProxy {
        let (network_ctx, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkContextMarker>()
                .expect("failed to create network context proxy");
        let () = sandbox.get_network_context(server).expect("calling get network context");
        let (endpoint_mgr, server) =
            fidl::endpoints::create_proxy::<fnetemul_network::EndpointManagerMarker>()
                .expect("failed to create endpoint manager proxy");
        let () = network_ctx.get_endpoint_manager(server).expect("calling get endpoint manager");
        let (status, endpoint) =
            endpoint_mgr.create_endpoint(name, &mut config).await.expect("calling create endpoint");
        let () = zx::Status::ok(status).expect("endpoint creation");
        endpoint.expect("endpoint creation").into_proxy().expect("failed to create endpoint proxy")
    }

    fn get_device_proxy(
        endpoint: &fnetemul_network::EndpointProxy,
    ) -> fidl::endpoints::ClientEnd<fnetemul_network::DeviceProxy_Marker> {
        let (device_proxy, server) =
            fidl::endpoints::create_endpoints::<fnetemul_network::DeviceProxy_Marker>()
                .expect("failed to create device proxy endpoints");
        let () = endpoint
            .get_proxy_(server)
            .expect("failed to get device proxy from netdevice endpoint");
        device_proxy
    }

    async fn get_devfs_watcher(realm: &fnetemul::ManagedRealmProxy) -> fvfs_watcher::Watcher {
        let (devfs, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory marker");
        let () = realm.get_devfs(server).expect("calling get devfs");
        fvfs_watcher::Watcher::new(devfs).await.expect("watcher creation")
    }

    async fn wait_for_event_on_path(
        watcher: &mut fvfs_watcher::Watcher,
        event: fvfs_watcher::WatchEvent,
        path: std::path::PathBuf,
    ) {
        let () = watcher
            .try_filter_map(|fvfs_watcher::WatchMessage { event: actual, filename }| {
                futures::future::ok((actual == event && filename == path).then(|| ()))
            })
            .try_next()
            .await
            .expect("error watching directory")
            .unwrap_or_else(|| {
                panic!("watcher stream expired before expected event {:?} was observed", event)
            });
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn devfs(sandbox: fnetemul::SandboxProxy) {
        let TestRealm { realm } = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let mut watcher = get_devfs_watcher(&realm).await;

        const TEST_DEVICE_NAME: &str = "test";
        let format_topological_path =
            |backing: &fnetemul_network::EndpointBacking, device_name: &str| match backing {
                fnetemul_network::EndpointBacking::Ethertap => {
                    format!("@/dev/test/tapctl/{}/ethernet", device_name)
                }
                fnetemul_network::EndpointBacking::NetworkDevice => {
                    format!("/netemul/{}", device_name)
                }
            };
        let backings = [
            fnetemul_network::EndpointBacking::Ethertap,
            fnetemul_network::EndpointBacking::NetworkDevice,
        ];
        for (i, backing) in backings.iter().enumerate() {
            let name = format!("{}{}", TEST_DEVICE_NAME, i);
            let endpoint = create_endpoint(
                &sandbox,
                &name,
                fnetemul_network::EndpointConfig { mtu: 1500, mac: None, backing: *backing },
            )
            .await;

            let () = realm
                .add_device(&name, get_device_proxy(&endpoint))
                .await
                .expect("calling add device")
                .map_err(zx::Status::from_raw)
                .expect("error adding device");
            let () = wait_for_event_on_path(
                &mut watcher,
                fvfs_watcher::WatchEvent::ADD_FILE,
                std::path::PathBuf::from(&name),
            )
            .await;
            assert_eq!(
                realm
                    .add_device(&name, get_device_proxy(&endpoint))
                    .await
                    .expect("calling add device")
                    .map_err(zx::Status::from_raw)
                    .expect_err("adding a duplicate device should fail"),
                zx::Status::ALREADY_EXISTS,
            );

            // Expect the device to implement `fuchsia.device/Controller.GetTopologicalPath`.
            let (controller, server_end) = zx::Channel::create().expect("failed to create channel");
            let () = get_device_proxy(&endpoint)
                .into_proxy()
                .expect("failed to create device proxy from client end")
                .serve_device(server_end)
                .expect("failed to serve device");
            let controller =
                fidl::endpoints::ClientEnd::<fdevice::ControllerMarker>::new(controller)
                    .into_proxy()
                    .expect("failed to create controller proxy from channel");
            let path = controller
                .get_topological_path()
                .await
                .expect("calling get topological path")
                .map_err(zx::Status::from_raw)
                .expect("failed to get topological path");
            assert_eq!(path, format_topological_path(backing, &name));

            let () = realm
                .remove_device(&name)
                .await
                .expect("calling remove device")
                .map_err(zx::Status::from_raw)
                .expect("error removing device");
            let () = wait_for_event_on_path(
                &mut watcher,
                fvfs_watcher::WatchEvent::REMOVE_FILE,
                std::path::PathBuf::from(&name),
            )
            .await;
            assert_eq!(
                realm
                    .remove_device(&name)
                    .await
                    .expect("calling remove device")
                    .map_err(zx::Status::from_raw)
                    .expect_err("removing a nonexistent device should fail"),
                zx::Status::NOT_FOUND,
            );
        }
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn devfs_per_realm(sandbox: fnetemul::SandboxProxy) {
        const TEST_DEVICE_NAME: &str = "test";
        let endpoint = create_endpoint(
            &sandbox,
            TEST_DEVICE_NAME,
            fnetemul_network::EndpointConfig {
                mtu: 1500,
                mac: None,
                backing: fnetemul_network::EndpointBacking::NetworkDevice,
            },
        )
        .await;
        let (TestRealm { realm: realm_a }, TestRealm { realm: realm_b }) = (
            TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(vec![counter_component()]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            ),
            TestRealm::new(
                &sandbox,
                fnetemul::RealmOptions {
                    children: Some(vec![counter_component()]),
                    ..fnetemul::RealmOptions::EMPTY
                },
            ),
        );
        let mut watcher_a = get_devfs_watcher(&realm_a).await;
        let () = realm_a
            .add_device(TEST_DEVICE_NAME, get_device_proxy(&endpoint))
            .await
            .expect("calling add device")
            .map_err(zx::Status::from_raw)
            .expect("error adding device");
        let () = wait_for_event_on_path(
            &mut watcher_a,
            fvfs_watcher::WatchEvent::ADD_FILE,
            std::path::PathBuf::from(TEST_DEVICE_NAME),
        )
        .await;
        // Expect not to see a matching device in `realm_b`'s devfs.
        let devfs_b = {
            let (devfs, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                .expect("create directory marker");
            let () = realm_b.get_devfs(server).expect("calling get devfs");
            devfs
        };
        let (status, mut buf) =
            devfs_b.read_dirents(fio::MAX_BUF).await.expect("calling read dirents");
        let () = zx::Status::ok(status).expect("failed reading directory entries");
        assert_eq!(
            files_async::parse_dir_entries(&mut buf)
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("failed parsing directory entries"),
            vec![files_async::DirEntry {
                name: ".".to_string(),
                kind: files_async::DirentKind::Directory
            }],
        );
    }
}
