// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _},
    fidl::endpoints::{DiscoverableProtocolMarker, ServerEnd},
    fidl_fuchsia_component_test as ftest, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fidl_fuchsia_logger as flogger, fidl_fuchsia_net_tun as fnet_tun,
    fidl_fuchsia_netemul::{
        self as fnetemul, ChildDef, ChildUses, ManagedRealmMarker, ManagedRealmRequest,
        RealmOptions, SandboxRequest, SandboxRequestStream,
    },
    fidl_fuchsia_netemul_network as fnetemul_network, fidl_fuchsia_sys2 as fsys2,
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    fuchsia_component_test::{
        self as fcomponent, Capability, ChildOptions, LocalComponentHandles, RealmBuilder,
        RealmInstance, Ref, Route,
    },
    fuchsia_driver_test::DriverTestRealmBuilder as _,
    fuchsia_zircon as zx,
    futures::pin_mut,
    futures::{
        channel::mpsc, FutureExt as _, SinkExt as _, StreamExt as _, TryFutureExt as _,
        TryStreamExt as _,
    },
    std::{
        borrow::Cow,
        collections::hash_map::{Entry, HashMap},
        sync::{
            atomic::{AtomicU64, Ordering},
            Arc,
        },
    },
    thiserror::Error,
    tracing::{debug, error, info, warn},
    vfs::directory::{
        entry::DirectoryEntry, helper::DirectlyMutable as _,
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
    #[error("source not provided")]
    SourceNotProvided,
    #[error("name not provided")]
    NameNotProvided,
    #[error("capability source not provided")]
    CapabilitySourceNotProvided,
    #[error("capability name not provided")]
    CapabilityNameNotProvided,
    #[error("duplicate capability '{0}' used by component '{1}'")]
    DuplicateCapabilityUse(String, String),
    #[error("cannot modify program arguments of component without a program: '{0}'")]
    ModifiedNonexistentProgram(String),
    #[error("realm builder error: {0:?}")]
    RealmBuilderError(#[from] fcomponent::error::Error),
    #[error("storage capability variant not provided")]
    StorageCapabilityVariantNotProvided,
    #[error("storage capability path not provided")]
    StorageCapabilityPathNotProvided,
    #[error("devfs capability name not provided")]
    DevfsCapabilityNameNotProvided,
    #[error("invalid devfs subdirectory '{0}'")]
    InvalidDevfsSubdirectory(String),
}

impl Into<zx::Status> for CreateRealmError {
    fn into(self) -> zx::Status {
        match self {
            CreateRealmError::SourceNotProvided
            | CreateRealmError::NameNotProvided
            | CreateRealmError::CapabilitySourceNotProvided
            | CreateRealmError::CapabilityNameNotProvided
            | CreateRealmError::DuplicateCapabilityUse(String { .. }, String { .. })
            | CreateRealmError::ModifiedNonexistentProgram(String { .. })
            | CreateRealmError::StorageCapabilityVariantNotProvided
            | CreateRealmError::StorageCapabilityPathNotProvided
            | CreateRealmError::DevfsCapabilityNameNotProvided
            | CreateRealmError::InvalidDevfsSubdirectory(String { .. }) => zx::Status::INVALID_ARGS,
            CreateRealmError::RealmBuilderError(error) => match error {
                // The following types of errors from the realm builder library are likely due to
                // client error (e.g. attempting to create a realm with an invalid configuration).
                fcomponent::error::Error::ServerError(
                    ftest::RealmBuilderError::ChildAlreadyExists
                    | ftest::RealmBuilderError::InvalidManifestExtension
                    | ftest::RealmBuilderError::InvalidComponentDecl
                    | ftest::RealmBuilderError::NoSuchChild
                    | ftest::RealmBuilderError::ChildDeclNotVisible
                    | ftest::RealmBuilderError::NoSuchSource
                    | ftest::RealmBuilderError::NoSuchTarget
                    | ftest::RealmBuilderError::CapabilitiesEmpty
                    | ftest::RealmBuilderError::TargetsEmpty
                    | ftest::RealmBuilderError::SourceAndTargetMatch
                    | ftest::RealmBuilderError::DeclNotFound
                    | ftest::RealmBuilderError::CapabilityInvalid
                    | ftest::RealmBuilderError::ImmutableProgram,
                ) => zx::Status::INVALID_ARGS,
                // The following types of realm builder errors are unlikely to be attributable to
                // the client, and are more likely to indicate e.g. a transport error or an
                // unexpected failure in the underlying system.
                fcomponent::error::Error::FidlError(e) => {
                    let _: fidl::Error = e;
                    zx::Status::INTERNAL
                }
                fcomponent::error::Error::FailedToOpenPkgDir(_)
                | fcomponent::error::Error::ConnectToServer(anyhow::Error { .. })
                | fcomponent::error::Error::FailedToCreateChild(anyhow::Error { .. })
                | fcomponent::error::Error::FailedToDestroyChild(anyhow::Error { .. })
                | fcomponent::error::Error::FailedToBind(anyhow::Error { .. }) => {
                    zx::Status::INTERNAL
                }
                fcomponent::error::Error::ServerError(e) => {
                    let _: ftest::RealmBuilderError = e;
                    zx::Status::INTERNAL
                }
                fcomponent::error::Error::RefUsedInWrongRealm(
                    fcomponent::Ref { .. },
                    String { .. },
                ) => zx::Status::INTERNAL,
                fcomponent::error::Error::DestroyWaiterTaken
                | fcomponent::error::Error::MissingSource
                | fcomponent::error::Error::LegacyChildrenUnsupportedInNestedComponentManager
                | fcomponent::error::Error::CannotStartRootComponent(_) => zx::Status::INTERNAL,
            },
        }
    }
}

struct StorageVariant(fnetemul::StorageVariant);

impl std::fmt::Display for StorageVariant {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let v = match self {
            Self(fnetemul::StorageVariant::Data) => "data",
            Self(fnetemul::StorageVariant::Cache) => "cache",
        };
        write!(f, "{}", v)
    }
}

#[derive(Debug, Eq, Hash, PartialEq)]
enum UniqueCapability<'a> {
    DevFs { name: Cow<'a, str> },
    Protocol { proto_name: Cow<'a, str> },
    Storage { mount_path: Cow<'a, str> },
}

impl<'a> UniqueCapability<'a> {
    fn new_protocol<P: DiscoverableProtocolMarker>() -> Self {
        Self::Protocol { proto_name: P::PROTOCOL_NAME.into() }
    }
}

async fn create_realm_instance(
    RealmOptions { name, children, .. }: RealmOptions,
    prefix: &str,
    network_realm: Arc<RealmInstance>,
    devfs: Arc<SimpleMutableDir>,
    devfs_proxy: fio::DirectoryProxy,
) -> Result<RealmInstance, CreateRealmError> {
    // Keep track of all the protocols exposed by components in the test realm, so that we can
    // prevent two components from exposing the same protocol to the root of the realm.
    let mut exposed_protocols = HashMap::new();
    // Keep track of dependencies between child components in the test realm in order to create the
    // relevant routes at the end. RealmBuilder doesn't allow creating routes between components if
    // the components haven't both been created yet, so we wait until all components have been
    // created to add routes between them.
    let mut child_dep_routes = Vec::new();
    // Keep track of all components with modified program arguments, so that once the realm is built
    // those components can be extracted and modified.
    let mut modified_program_args = HashMap::new();

    let builder = RealmBuilder::new_with_collection(REALM_COLLECTION_NAME.to_string()).await?;
    let netemul_services = builder
        .add_local_child(
            NETEMUL_SERVICES_COMPONENT_NAME,
            move |handles: LocalComponentHandles| {
                Box::pin(run_netemul_services(
                    handles,
                    network_realm.clone(),
                    Clone::clone(&devfs_proxy),
                ))
            },
            ChildOptions::new(),
        )
        .await?;
    for ChildDef { source, name, exposes, uses, program_args, eager, .. } in
        children.unwrap_or_default()
    {
        let source = source.ok_or(CreateRealmError::SourceNotProvided)?;
        let name = name.ok_or(CreateRealmError::NameNotProvided)?;
        let mut child = ChildOptions::new();
        if eager.unwrap_or(false) {
            child = child.eager();
        }
        let child_ref = match source {
            fnetemul::ChildSource::Component(url) => builder.add_child(&name, &url, child).await?,
            fnetemul::ChildSource::Mock(dir) => {
                let dir = dir.into_proxy().expect("failed to create proxy from channel");
                builder
                    .add_local_child(
                        &name,
                        move |mock_handles: LocalComponentHandles| {
                            futures::future::ready(
                                dir.clone(
                                    fio::OpenFlags::CLONE_SAME_RIGHTS,
                                    mock_handles.outgoing_dir.into_channel().into(),
                                )
                                .context("cloning directory for mock handles"),
                            )
                            // The lifetime of the mock child component is tied
                            // to that of this future. Make the future never
                            // return, so that the mock child is kept alive.
                            .and_then(|()| futures::future::pending())
                            .boxed()
                        },
                        child,
                    )
                    .await?
            }
        };
        if let Some(program_args) = program_args {
            // This assertion should always pass because `RealmBuilder::add_child` will have
            // failed already if a component with the same moniker already exists in the realm.
            assert_eq!(modified_program_args.insert(name.clone(), program_args), None);
        }
        if let Some(exposes) = exposes {
            for exposed in exposes {
                // TODO(https://fxbug.dev/72043): allow duplicate protocols.
                //
                // Protocol names will be aliased as `child_name/protocol_name`, and this panic will
                // be replaced with an INVALID_ARGS epitaph sent on the `ManagedRealm` channel if a
                // child component with a duplicate name is created, or if a child exposes two
                // protocols of the same name.
                match exposed_protocols.entry(exposed) {
                    std::collections::hash_map::Entry::Occupied(entry) => {
                        panic!(
                            "duplicate protocol name '{}' exposed from component '{}'",
                            entry.key(),
                            entry.get(),
                        );
                    }
                    std::collections::hash_map::Entry::Vacant(entry) => {
                        let () = builder
                            .add_route(
                                Route::new()
                                    .capability(Capability::protocol_by_name(entry.key()))
                                    .from(&child_ref)
                                    .to(Ref::parent()),
                            )
                            .await?;
                        let _: &mut String = entry.insert(name.clone());
                    }
                }
            }
        }
        if let Some(uses) = uses {
            match uses {
                ChildUses::Capabilities(caps) => {
                    // TODO(https://github.com/rust-lang/rust/issues/60896): use std's HashSet.
                    type HashSet<T> = HashMap<T, ()>;
                    let mut unique_caps = HashSet::new();
                    for cap in caps {
                        // TODO(https://fxbug.dev/77069): consider introducing an abstraction here
                        // over the (fnetemul::Capability, CapabilityRoute, String) triple that is
                        // defined here for each of the built-in netemul capabilities, corresponding
                        // to their FIDL representation, routing logic, and capability name.
                        let cap = match cap {
                            fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                                name: capability_name,
                                subdir,
                                ..
                            }) => {
                                let capability_name = capability_name
                                    .ok_or(CreateRealmError::DevfsCapabilityNameNotProvided)?;
                                if let Some(subdir) = subdir.as_ref() {
                                    let _: Arc<SimpleMutableDir> = open_or_create_dir(
                                        devfs.clone(),
                                        &std::path::Path::new(subdir),
                                    )
                                    .await
                                    .map_err(|e| {
                                        error!(
                                            "failed to create subdirectory '{}' in devfs: {}",
                                            subdir, e
                                        );
                                        CreateRealmError::InvalidDevfsSubdirectory(
                                            subdir.to_string(),
                                        )
                                    })?;
                                }
                                let mut capability = Capability::directory(DEVFS)
                                    // TODO(https://fxbug.dev/77059): remove write permissions once
                                    // they are no longer required to connect to services.
                                    .rights(fio::RW_STAR_DIR)
                                    .path(DEVFS_PATH)
                                    .as_(capability_name.clone());
                                if let Some(subdir) = subdir {
                                    capability = capability.subdir(subdir);
                                }
                                builder
                                    .add_route(
                                        Route::new()
                                            .capability(capability)
                                            .from(&netemul_services)
                                            .to(&child_ref),
                                    )
                                    .await?;
                                UniqueCapability::DevFs { name: capability_name.into() }
                            }
                            fnetemul::Capability::NetemulNetworkContext(fnetemul::Empty {}) => {
                                builder
                                    .add_route(
                                        Route::new()
                                            .capability(Capability::protocol::<
                                                fnetemul_network::NetworkContextMarker,
                                            >(
                                            ))
                                            .from(&netemul_services)
                                            .to(&child_ref),
                                    )
                                    .await?;
                                UniqueCapability::new_protocol::<
                                    fnetemul_network::NetworkContextMarker,
                                >()
                            }
                            fnetemul::Capability::LogSink(fnetemul::Empty {}) => {
                                builder
                                    .add_route(
                                        Route::new()
                                            .capability(Capability::protocol::<
                                                flogger::LogSinkMarker,
                                            >(
                                            ))
                                            .from(Ref::parent())
                                            .to(&child_ref),
                                    )
                                    .await?;
                                UniqueCapability::new_protocol::<flogger::LogSinkMarker>()
                            }
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                name: source,
                                capability,
                                ..
                            }) => {
                                let source =
                                    source.ok_or(CreateRealmError::CapabilitySourceNotProvided)?;
                                let fnetemul::ExposedCapability::Protocol(capability) = capability
                                    .ok_or(CreateRealmError::CapabilityNameNotProvided)?;
                                debug!(
                                    "routing capability '{}' from component '{}' to '{}'",
                                    capability, source, name
                                );
                                let () = child_dep_routes.push(
                                    Route::new()
                                        .capability(Capability::protocol_by_name(&capability))
                                        .from(Ref::child(source))
                                        .to(&child_ref),
                                );
                                UniqueCapability::Protocol { proto_name: capability.into() }
                            }
                            fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                                variant,
                                path,
                                ..
                            }) => {
                                let variant = variant
                                    .ok_or(CreateRealmError::StorageCapabilityVariantNotProvided)?;
                                let variant = StorageVariant(variant);
                                let mount_path =
                                    path.ok_or(CreateRealmError::StorageCapabilityPathNotProvided)?;
                                let () = builder
                                    .add_route(
                                        Route::new()
                                            .capability(
                                                Capability::storage(variant.to_string())
                                                    .path(mount_path.to_string()),
                                            )
                                            .from(Ref::parent())
                                            .to(&child_ref),
                                    )
                                    .await?;
                                UniqueCapability::Storage { mount_path: mount_path.into() }
                            }
                        };
                        match unique_caps.entry(cap) {
                            Entry::Occupied(entry) => {
                                let (cap, ()) = entry.remove_entry();
                                return Err(CreateRealmError::DuplicateCapabilityUse(
                                    format!("{:?}", cap),
                                    name,
                                ));
                            }
                            Entry::Vacant(entry) => {
                                let () = entry.insert(());
                            }
                        }
                    }
                }
            }
        }
    }
    for route in child_dep_routes {
        let () = builder.add_route(route).await?;
    }
    // Override the program args section of the component declaration for components that specified
    // args.
    for (component, program_args) in modified_program_args {
        let mut decl = builder.get_component_decl(component.as_str()).await?;
        let cm_rust::ComponentDecl { program, .. } = &mut decl;
        // Create `program` if it is None.
        let cm_rust::ProgramDecl { runner: _, info } = if let Some(program) = program.as_mut() {
            program
        } else {
            return Err(CreateRealmError::ModifiedNonexistentProgram(component));
        };
        let fdata::Dictionary { ref mut entries, .. } = info;
        // Create `entries` if it is None.
        let entries = entries.get_or_insert_with(|| Vec::default());
        // Create an "args" entry if there is none and replace whatever is currently in the "args"
        // entry with the program arguments passed in.
        const ARGS_KEY: &str = "args";
        let args_value = Some(Box::new(fdata::DictionaryValue::StrVec(program_args)));
        match entries.iter_mut().find_map(
            |fdata::DictionaryEntry { key, value }| {
                if key == ARGS_KEY {
                    Some(value)
                } else {
                    None
                }
            },
        ) {
            Some(args) => *args = args_value,
            None => {
                let () = entries
                    .push(fdata::DictionaryEntry { key: ARGS_KEY.to_string(), value: args_value });
            }
        };
        let () = builder.replace_component_decl(component.as_str(), decl).await?;
    }
    let () = builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fsys2::LifecycleControllerMarker>())
                .from(Ref::framework())
                .to(Ref::parent()),
        )
        .await?;

    let name =
        name.map(|name| format!("{}-{}", prefix, name)).unwrap_or_else(|| prefix.to_string());
    info!("creating new ManagedRealm with name '{}'", name);
    builder.build_with_name(name).await.map_err(Into::into)
}

struct ManagedRealm {
    server_end: ServerEnd<ManagedRealmMarker>,
    realm: RealmInstance,
    devfs: Arc<SimpleMutableDir>,
}

fn with_responder_ignoring_peer_closed<R, F>(r: R, f: F) -> Result<(), fidl::Error>
where
    R: fidl::endpoints::Responder,
    F: FnOnce(R) -> Result<(), fidl::Error>,
{
    match f(r) {
        Ok(()) => Ok(()),
        // If the client closed the channel, log a warning and do not propagate the error.
        Err(e) if e.is_closed() => {
            warn!("client closed managed realm channel with request(s) in flight: {:?}", e);
            Ok(())
        }
        Err(e) => Err(e),
    }
}

impl ManagedRealm {
    async fn run_service(self) -> Result {
        let Self { server_end, realm, devfs } = self;
        let mut stream = server_end.into_stream().context("failed to acquire request stream")?;
        while let Some(request) = stream.try_next().await.context("FIDL error")? {
            match request {
                ManagedRealmRequest::GetMoniker { responder } => {
                    let moniker = format!("{}:{}", REALM_COLLECTION_NAME, realm.root.child_name());
                    with_responder_ignoring_peer_closed(responder, |r| r.send(&moniker))
                        .context("responding to GetMoniker request")?;
                }
                ManagedRealmRequest::ConnectToProtocol {
                    protocol_name,
                    child_name,
                    req,
                    control_handle: _,
                } => {
                    // TODO(https://fxbug.dev/72043): allow `child_name` to be specified once we
                    // prefix capabilities with the name of the component exposing them.
                    //
                    // Currently `child_name` isn't used to disambiguate duplicate protocols, so we
                    // don't allow it to be specified.
                    if let Some(_) = child_name {
                        todo!("allow `child_name` to be specified in `ConnectToProtocol` request");
                    }
                    debug!(
                        "connecting to protocol `{}` exposed by child `{:?}`",
                        protocol_name, child_name
                    );
                    let () = realm
                        .root
                        .connect_request_to_named_protocol_at_exposed_dir(&protocol_name, req)
                        .with_context(|| {
                            format!("failed to open protocol {} in directory", protocol_name)
                        })?;
                }
                ManagedRealmRequest::GetDevfs { devfs: server_end, control_handle: _ } => {
                    let () = devfs.clone().open(
                        vfs::execution_scope::ExecutionScope::new(),
                        fio::OpenFlags::RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        vfs::path::Path::dot(),
                        server_end.into_channel().into(),
                    );
                }
                ManagedRealmRequest::AddDevice { path, device, responder } => {
                    // ClientEnd::into_proxy should only return an Err when there is no executor, so
                    // this is not expected to ever cause a panic.
                    let device = device.into_proxy().expect("failed to get device proxy");
                    let devfs = devfs.clone();
                    let response = (|| async move {
                        let (parent_path, device_name) =
                            split_path_into_dir_and_file_name(&std::path::Path::new(&path))
                                .map_err(|e| {
                                    error!(
                                        "failed to split path '{}' into directory and filename: {}",
                                        path, e
                                    );
                                    zx::Status::INVALID_ARGS
                                })?;
                        let dir = open_or_create_dir(devfs, parent_path).await.map_err(|e| {
                            error!("failed to open or create path '{}': {}", path, e);
                            zx::Status::INVALID_ARGS
                        })?;
                        let path_clone = path.clone();
                        let response = dir.add_entry(
                            device_name,
                            vfs::service::endpoint(
                                move |_: vfs::execution_scope::ExecutionScope, channel| {
                                    let () = device
                                        .clone()
                                        .serve_device(channel.into_zx_channel())
                                        .unwrap_or_else(|e| {
                                            // PEER_CLOSED errors are expected
                                            // to happen during test teardown.
                                            if e.is_closed() {
                                                warn!(
                                                    "failed to serve device on path {}: {}",
                                                    path_clone, e
                                                );
                                            } else {
                                                error!(
                                                    "failed to serve device on path {}: {}",
                                                    path_clone, e
                                                );
                                            }
                                        });
                                },
                            ),
                        );
                        match response {
                            Ok(()) => {
                                info!("adding virtual device at path '{}/{}'", DEVFS_PATH, path)
                            }
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
                        response
                    })()
                    .await;
                    with_responder_ignoring_peer_closed(responder, |r| {
                        r.send(&mut response.map_err(zx::Status::into_raw))
                    })
                    .context("responding to AddDevice request")?;
                }
                ManagedRealmRequest::RemoveDevice { path, responder } => {
                    let devfs = devfs.clone();
                    let response = (|| async move {
                        let (parent_path, device_name) =
                            split_path_into_dir_and_file_name(&std::path::Path::new(&path))
                                .map_err(|e| {
                                    error!(
                                        "failed to split path '{}' into directory and filename: {}",
                                        path, e
                                    );
                                    zx::Status::INVALID_ARGS
                                })?;
                        let dir = open_or_create_dir(devfs, parent_path).await.map_err(|e| {
                            error!("failed to open or create path '{}': {}", path, e);
                            zx::Status::INVALID_ARGS
                        })?;
                        let response = match dir.remove_entry(device_name, false) {
                            Ok(entry) => {
                                if let Some(entry) = entry {
                                    let _: Arc<dyn vfs::directory::entry::DirectoryEntry> = entry;
                                    info!(
                                        "removing virtual device at path '{}/{}'",
                                        DEVFS_PATH, path
                                    );
                                    Ok(())
                                } else {
                                    warn!(
                                        "cannot remove device at path '{}/{}': path is not \
                                        currently bound to a device",
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
                        response
                    })()
                    .await;
                    with_responder_ignoring_peer_closed(responder, |r| {
                        r.send(&mut response.map_err(zx::Status::into_raw))
                    })
                    .context("responding to RemoveDevice request")?;
                }
                ManagedRealmRequest::StopChildComponent { child_name, responder } => {
                    let realm_ref = &realm;
                    let response = async move {
                        let lifecycle =
                            fuchsia_component::client::connect_to_protocol_at_dir_root::<
                                fsys2::LifecycleControllerMarker,
                            >(realm_ref.root.get_exposed_dir())
                            .map_err(|e: anyhow::Error| {
                                error!("failed to open proxy to lifecycle controller: {}", e);
                                Err(zx::Status::INTERNAL)
                            })?;
                        let () = lifecycle
                            .stop(&format!("./{}", child_name), true)
                            .await
                            .map_err(|e: fidl::Error| {
                                error!("fidl call to LifecycleController/Stop failed: {}", e);
                                Err(zx::Status::INTERNAL)
                            })?
                            .map_err(|e: fidl_fuchsia_component::Error| {
                                warn!("failed to stop child component '{}': {:?}", child_name, e);
                                match e {
                                    fidl_fuchsia_component::Error::InvalidArguments => {
                                        Err(zx::Status::INVALID_ARGS)
                                    }
                                    fidl_fuchsia_component::Error::AccessDenied => {
                                        Err(zx::Status::ACCESS_DENIED)
                                    }
                                    fidl_fuchsia_component::Error::InstanceCannotResolve => {
                                        Err(zx::Status::UNAVAILABLE)
                                    }
                                    fidl_fuchsia_component::Error::InstanceCannotUnresolve => {
                                        Err(zx::Status::UNAVAILABLE)
                                    }
                                    fidl_fuchsia_component::Error::InstanceNotFound => {
                                        Err(zx::Status::NOT_FOUND)
                                    }
                                    fidl_fuchsia_component::Error::Internal
                                    | fidl_fuchsia_component::Error::Unsupported
                                    | fidl_fuchsia_component::Error::InstanceAlreadyExists
                                    | fidl_fuchsia_component::Error::InstanceCannotStart
                                    | fidl_fuchsia_component::Error::CollectionNotFound
                                    | fidl_fuchsia_component::Error::ResourceUnavailable
                                    | fidl_fuchsia_component::Error::InstanceDied
                                    | fidl_fuchsia_component::Error::ResourceNotFound => {
                                        Err(zx::Status::INTERNAL)
                                    }
                                }
                            })?;
                        Ok(())
                    }
                    .await;
                    with_responder_ignoring_peer_closed(responder, |r| {
                        r.send(&mut response.map_err(zx::Status::into_raw))
                    })
                    .context("responding to StopChildComponent request")?;
                }
                ManagedRealmRequest::Shutdown { control_handle } => {
                    let () = realm.destroy().await.context("destroy realm")?;
                    let () = control_handle
                        .send_on_shutdown()
                        .unwrap_or_else(|e| error!("failed to send OnShutdown event: {:?}", e));
                    break;
                }
            }
        }
        Ok(())
    }
}

fn split_path_into_dir_and_file_name<'a>(
    path: &'a std::path::Path,
) -> Result<(&'a std::path::Path, &'a str)> {
    let file_name = path
        .file_name()
        .context("path does not end in a normal file or directory name")?
        .to_str()
        .context("invalid file name")?;
    let parent = path.parent().context("path terminates in a root")?;
    Ok((parent, file_name))
}

async fn open_or_create_dir(
    root: Arc<SimpleMutableDir>,
    path: &std::path::Path,
) -> Result<Arc<SimpleMutableDir>> {
    let root = futures::stream::iter(path.components())
        .map(Ok)
        .try_fold(root, |root, component| async move {
            let entry = match component {
                std::path::Component::Prefix(_) | std::path::Component::ParentDir => {
                    Err(anyhow!("path cannot contain prefix or parent component ('..')"))
                }
                component => component.as_os_str().to_str().context("invalid path component"),
            }?;
            // Get a handle to the entry, and create it if it doesn't already exist.
            let entry = match root.get_entry(entry) {
                Ok(entry) => entry,
                Err(status) => match status {
                    zx::Status::NOT_FOUND => {
                        let () = root
                            .add_entry(entry, vfs::directory::mutable::simple::simple())
                            .context("failed to add directory entry")?;
                        root.get_entry(entry).context("failed to get directory entry")?
                    }
                    status => {
                        return Err(anyhow!(
                            "got unexpected error on get entry '{}': expected {}, got {}",
                            entry,
                            zx::Status::NOT_FOUND,
                            status,
                        ));
                    }
                },
            };
            // Downcast the entry to a directory so that we can perform directory operations on it.
            Ok(entry
                .into_any()
                .downcast::<SimpleMutableDir>()
                .expect("could not downcast entry to a directory"))
        })
        .await?;
    Ok(root)
}

async fn run_netemul_services(
    handles: LocalComponentHandles,
    network_realm: impl std::ops::Deref<Target = RealmInstance> + 'static,
    devfs: fio::DirectoryProxy,
) -> Result {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> = fs
        .dir("svc")
        .add_service_at(fnetemul_network::NetworkContextMarker::PROTOCOL_NAME, |channel| {
            Some(ServerEnd::<fnetemul_network::NetworkContextMarker>::new(channel))
        });
    let () = fs.add_remote(DEVFS, devfs);
    let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir)?;
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
        .context("create directory proxy")?;
    let dir = vfs::directory::mutable::simple::simple();
    let () = dir.clone().open(
        vfs::execution_scope::ExecutionScope::new(),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        fio::MODE_TYPE_DIRECTORY,
        vfs::path::Path::dot(),
        server.into_channel().into(),
    );
    Ok((proxy, dir))
}

const NETWORK_CONTEXT_COMPONENT_NAME: &str = "network-context";

async fn setup_network_realm(sandbox_name: impl std::fmt::Display) -> Result<RealmInstance> {
    let relative_url = |component_name: &str| format!("#meta/{}.cm", component_name);
    let network_context_package_url = relative_url(NETWORK_CONTEXT_COMPONENT_NAME);

    let builder = RealmBuilder::new_with_collection(REALM_COLLECTION_NAME.to_string())
        .await
        .context("error creating new realm builder")?;
    let _: &RealmBuilder = builder
        .driver_test_realm_setup()
        .await
        .context("error setting up the driver test realm")?;
    let network_context_child = builder
        .add_child(NETWORK_CONTEXT_COMPONENT_NAME, network_context_package_url, ChildOptions::new())
        .await
        .context("error adding network-context component")?;
    let () = builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fnetemul_network::NetworkContextMarker>())
                .from(&network_context_child)
                .to(Ref::parent()),
        )
        .await
        .with_context(|| {
            format!(
                "error adding route exposing capability '{}' from component '{}'",
                fnetemul_network::NetworkContextMarker::PROTOCOL_NAME,
                NETWORK_CONTEXT_COMPONENT_NAME
            )
        })?;
    let () = builder
        .add_route(
            Route::new()
                .capability(Capability::directory(DEVFS).path(DEVFS_PATH).rights(fio::R_STAR_DIR))
                .capability(Capability::protocol::<fidl_fuchsia_driver_test::RealmMarker>())
                .from(Ref::child(fuchsia_driver_test::COMPONENT_NAME))
                .to(&network_context_child),
        )
        .await
        .with_context(|| {
            format!(
                "error adding route offering directory 'dev' and protocol '{}' from component '{}' to '{}'",
                fidl_fuchsia_driver_test::RealmMarker::PROTOCOL_NAME,
                fuchsia_driver_test::COMPONENT_NAME,
                NETWORK_CONTEXT_COMPONENT_NAME
            )
        })?;
    let () = builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fnet_tun::ControlMarker>())
                .capability(Capability::protocol::<flogger::LogSinkMarker>())
                .from(Ref::parent())
                .to(&network_context_child),
        )
        .await
        .with_context(|| {
            format!(
                "error adding route offering capabilities to component '{}'",
                NETWORK_CONTEXT_COMPONENT_NAME
            )
        })?;
    let instance = builder
        .build_with_name(format!("{}-network-realm", sandbox_name))
        .await
        .context("error creating realm instance")?;
    Ok(instance)
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
                    match create_realm_instance(
                        options,
                        &prefix,
                        network_realm.clone(),
                        devfs.clone(),
                        proxy,
                    )
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
            }
            Ok(())
        }
    });
    let realms_fut = rx
        .for_each_concurrent(None, |realm| async {
            let name = realm.realm.root.child_name().to_owned();
            realm
                .run_service()
                .await
                .unwrap_or_else(|e| error!("error managing realm '{}': {:?}", name, e))
        })
        .fuse();
    pin_mut!(sandbox_fut, realms_fut);
    futures::select! {
        result = sandbox_fut => Ok(result?),
        () = realms_fut => unreachable!("realms_fut should never complete"),
    }
}

#[fuchsia::main()]
async fn main() -> Result {
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
        fidl_fuchsia_netemul as fnetemul, fidl_fuchsia_netemul_test as fnetemul_test,
        fidl_fuchsia_netemul_test::CounterMarker, fixture::fixture, fuchsia_async as fasync,
        fuchsia_vfs_watcher as fvfs_watcher, std::convert::TryFrom as _,
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

        fn connect_to_protocol<S: DiscoverableProtocolMarker>(&self) -> S::Proxy {
            let (proxy, server_end) = fidl::endpoints::create_proxy::<S>()
                .context(S::DEBUG_NAME)
                .expect("failed to create proxy");
            let () = self
                .realm
                .connect_to_protocol(S::PROTOCOL_NAME, None, server_end.into_channel())
                .context(S::DEBUG_NAME)
                .expect("failed to connect");
            proxy
        }
    }

    const COUNTER_COMPONENT_NAME: &str = "counter";
    const COUNTER_URL: &str = "#meta/counter.cm";
    const COUNTER_WITHOUT_PROGRAM_URL: &str = "#meta/counter-without-program.cm";
    const COUNTER_A_PROTOCOL_NAME: &str = "fuchsia.netemul.test.CounterA";
    const COUNTER_B_PROTOCOL_NAME: &str = "fuchsia.netemul.test.CounterB";
    const DATA_PATH: &str = "/data";
    const CACHE_PATH: &str = "/cache";

    fn counter_component() -> fnetemul::ChildDef {
        fnetemul::ChildDef {
            source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
            name: Some(COUNTER_COMPONENT_NAME.to_string()),
            exposes: Some(vec![CounterMarker::PROTOCOL_NAME.to_string()]),
            uses: Some(fnetemul::ChildUses::Capabilities(vec![fnetemul::Capability::LogSink(
                fnetemul::Empty {},
            )])),
            ..fnetemul::ChildDef::EMPTY
        }
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn can_connect_to_single_protocol(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();
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
        let counter_a = realm_a.connect_to_protocol::<CounterMarker>();
        let counter_b = realm_b.connect_to_protocol::<CounterMarker>();
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
        let counter = realm.connect_to_protocol::<CounterMarker>();
        assert_eq!(
            counter.increment().await.expect("fuchsia.netemul.test/Counter.increment call failed"),
            1,
        );
        drop(realm);
        assert_eq!(
            counter.on_closed().await,
            Ok(zx::Signals::CHANNEL_PEER_CLOSED),
            "`CounterProxy` should be closed when `ManagedRealmProxy` is dropped",
        );
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn shutdown_realm_destroys_children(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();
        assert_eq!(
            counter.increment().await.expect("fuchsia.netemul.test/Counter.increment call failed"),
            1,
        );
        let TestRealm { realm } = realm;
        let () = realm.shutdown().expect("failed to call shutdown");
        let events = realm
            .take_event_stream()
            .try_collect::<Vec<_>>()
            .await
            .expect("error on realm event stream");
        // Ensure there are no more events sent on the event stream after `OnShutdown`.
        assert_matches::assert_matches!(events[..], [fnetemul::ManagedRealmEvent::OnShutdown {}]);
        assert_eq!(
            counter.on_closed().await,
            Ok(zx::Signals::CHANNEL_PEER_CLOSED),
            "counter proxy should be closed when managed realm is shut down",
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
            let counter = realm.connect_to_protocol::<CounterMarker>();
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
                counter.on_closed().await,
                Ok(zx::Signals::CHANNEL_PEER_CLOSED),
                "`CounterProxy` should be closed when `SandboxProxy` is dropped",
            );
        }
        for realm in realms {
            let TestRealm { realm } = realm;
            assert_eq!(
                realm.on_closed().await,
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
            format!("{}:set_realm_name0-test-realm-name", REALM_COLLECTION_NAME),
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
                format!("{}:auto_generated_realm_name{}", REALM_COLLECTION_NAME, i),
            );
        }
    }

    async fn expect_single_inspect_node(
        realm: &TestRealm,
        component_moniker: &str,
        f: impl Fn(&diagnostics_hierarchy::DiagnosticsHierarchy),
    ) {
        let TestRealm { realm } = realm;
        let realm_moniker = realm.get_moniker().await.expect("failed to get moniker");
        let data = diagnostics_reader::ArchiveReader::new()
            .add_selector(diagnostics_reader::ComponentSelector::new(vec![
                selectors::sanitize_string_for_selectors(&realm_moniker),
                component_moniker.into(),
            ]))
            .snapshot::<diagnostics_reader::Inspect>()
            .await
            .expect("failed to get inspect data")
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
            [Some(datum)] => f(datum),
            data => panic!("there should be exactly one matching inspect node; found {:?}", data),
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
            let counter = realm.connect_to_protocol::<CounterMarker>();
            for j in 1..=i {
                assert_eq!(
                    counter.increment().await.expect(&format!(
                        "fuchsia.netemul.test/Counter.increment call failed on realm {}",
                        i
                    )),
                    j,
                );
            }
            let () = expect_single_inspect_node(&realm, COUNTER_COMPONENT_NAME, |data| {
                diagnostics_reader::assert_data_tree!(data, root: {
                    counter: {
                        count: u64::from(i),
                    }
                });
            })
            .await;
        }
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn eager_component(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![fnetemul::ChildDef {
                    eager: Some(true),
                    ..counter_component()
                }]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );

        // Connect to fuchsia.component.Binder to start the test realm.
        let binder_proxy = realm.connect_to_protocol::<fidl_fuchsia_component::BinderMarker>();

        // Receive Signal if fuchsia.component.Binder channel is closed prematurely.
        // This channel is scoped to the runtime of the component, so it should
        // not be closed before the component stops.
        let binder_fut = binder_proxy.on_closed().fuse();

        // Hold Future object of main assertion of the test so that we can join!
        // with the binder channel event stream below.
        let assert_fut =
            // Without binding to the child by connecting to its exposed protocol, we should be able to
            // see its inspect data since it has been started eagerly.
            expect_single_inspect_node(&realm, COUNTER_COMPONENT_NAME, |data| {
                diagnostics_reader::assert_data_tree!(data, root: {
                    counter: {
                        count: 0u64,
                    }
                });
            }).fuse();

        pin_mut!(binder_fut, assert_fut);

        futures::select! {
            () = assert_fut => {},
            signals = binder_fut => panic!("binder channel closed with: {:?}", signals)
        };
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
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        name: Some("counter-with-network-context".to_string()),
                        exposes: Some(vec![CounterMarker::PROTOCOL_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                            fnetemul::Capability::NetemulNetworkContext(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    // TODO(https://fxbug.dev/65359): when we can allow ERROR logs for routing
                    // errors, add a child component that does not `use` NetworkContext, and verify
                    // that we cannot get at NetworkContext through it. It should result in a
                    // zx::Status::UNAVAILABLE error.
                ]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();
        let (network_context, server_end) =
            fidl::endpoints::create_proxy::<fnetemul_network::NetworkContextMarker>()
                .expect("failed to create network context proxy");
        let () = counter
            .connect_to_protocol(
                fnetemul_network::NetworkContextMarker::PROTOCOL_NAME,
                server_end.into_channel(),
            )
            .expect("failed to connect to network context through counter");
        assert_matches::assert_matches!(
            network_context.setup(&mut Vec::new().iter_mut()).await,
            Ok((zx::sys::ZX_OK, Some(_setup_handle)))
        );
    }

    #[fixture(with_sandbox)]
    // TODO(https://fxbug.dev/65359): when we can allowlist particular ERROR logs in a test, we can
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
                name: "no source provided",
                children: vec![fnetemul::ChildDef {
                    source: None,
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "no name provided",
                children: vec![fnetemul::ChildDef {
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    name: None,
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "name not specified for child dependency",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                            name: None,
                            capability: Some(fnetemul::ExposedCapability::Protocol(
                                CounterMarker::PROTOCOL_NAME.to_string(),
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
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
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
                name: "duplicate capability used by child",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        fnetemul::Capability::LogSink(fnetemul::Empty {}),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "child manually depends on a duplicate of a netemul-provided capability",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                            name: Some("root".to_string()),
                            capability: Some(fnetemul::ExposedCapability::Protocol(
                                flogger::LogSinkMarker::PROTOCOL_NAME.to_string(),
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
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                // counter-a does not exist.
                                name: Some("counter-a".to_string()),
                                capability: Some(fnetemul::ExposedCapability::Protocol(
                                    CounterMarker::PROTOCOL_NAME.to_string(),
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
                name: "child depends on storage without variant",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                            variant: None,
                            path: Some(DATA_PATH.to_string()),
                            ..fnetemul::StorageDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "child depends on storage without path",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                            variant: Some(fnetemul::StorageVariant::Data),
                            path: None,
                            ..fnetemul::StorageDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "duplicate components",
                children: vec![
                    fnetemul::ChildDef {
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    fnetemul::ChildDef {
                        name: Some(COUNTER_COMPONENT_NAME.to_string()),
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        ..fnetemul::ChildDef::EMPTY
                    },
                ],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "storage capabilities use duplicate paths",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                            variant: Some(fnetemul::StorageVariant::Data),
                            path: Some(DATA_PATH.to_string()),
                            ..fnetemul::StorageDep::EMPTY
                        }),
                        fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                            variant: Some(fnetemul::StorageVariant::Data),
                            path: Some(DATA_PATH.to_string()),
                            ..fnetemul::StorageDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "devfs capability name not provided",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                            name: None,
                            ..fnetemul::DevfsDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "invalid subdirectory of devfs requested",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                            name: Some(DEVFS.to_string()),
                            subdir: Some("..".to_string()),
                            ..fnetemul::DevfsDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            TestCase {
                name: "dependency cycle between child components",
                children: vec![
                    fnetemul::ChildDef {
                        name: Some("counter-a".to_string()),
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                name: Some("counter-b".to_string()),
                                capability: Some(fnetemul::ExposedCapability::Protocol(
                                    CounterMarker::PROTOCOL_NAME.to_string(),
                                )),
                                ..fnetemul::ChildDep::EMPTY
                            }),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    fnetemul::ChildDef {
                        name: Some("counter-b".to_string()),
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                name: Some("counter-a".to_string()),
                                capability: Some(fnetemul::ExposedCapability::Protocol(
                                    CounterMarker::PROTOCOL_NAME.to_string(),
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
                name: "overriden program args for component without program",
                children: vec![fnetemul::ChildDef {
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    source: Some(fnetemul::ChildSource::Component(
                        COUNTER_WITHOUT_PROGRAM_URL.to_string(),
                    )),
                    program_args: Some(vec![]),
                    ..fnetemul::ChildDef::EMPTY
                }],
                epitaph: zx::Status::INVALID_ARGS,
            },
            // TODO(https://fxbug.dev/72043): once we allow duplicate protocols, verify that a child
            // exposing duplicate protocols results in a ZX_ERR_INTERNAL epitaph.
        ];
        for TestCase { name, children, epitaph } in cases {
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
                    protocol_name:
                        <ManagedRealmMarker as fidl::endpoints::ProtocolMarker>::DEBUG_NAME,
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
        let TestRealm { realm } = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![
                    fnetemul::ChildDef {
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        name: Some("counter-a".to_string()),
                        exposes: Some(vec![COUNTER_A_PROTOCOL_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                            fnetemul::Capability::ChildDep(fnetemul::ChildDep {
                                name: Some("counter-b".to_string()),
                                capability: Some(fnetemul::ExposedCapability::Protocol(
                                    COUNTER_B_PROTOCOL_NAME.to_string(),
                                )),
                                ..fnetemul::ChildDep::EMPTY
                            }),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    fnetemul::ChildDef {
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        name: Some("counter-b".to_string()),
                        exposes: Some(vec![COUNTER_B_PROTOCOL_NAME.to_string()]),
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
                .connect_to_protocol(COUNTER_A_PROTOCOL_NAME, None, server_end.into_channel())
                .expect("failed to connect to CounterA protocol");
            counter_a
        };
        // counter-a should have access to counter-b's exposed protocol.
        let (counter_b, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
            .expect("failed to create CounterB proxy");
        let () = counter_a
            .connect_to_protocol(COUNTER_B_PROTOCOL_NAME, server_end.into_channel())
            .expect("fuchsia.netemul.test/CounterA.connect_to_protocol call failed");
        assert_eq!(
            counter_b
                .increment()
                .await
                .expect("fuchsia.netemul.test/CounterB.increment call failed"),
            1,
        );
        // The counter-b protocol that counter-a has access to should be the same one accessible
        // through the test realm.
        let counter_b = {
            let (counter_b, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
                .expect("failed to create CounterB proxy");
            let () = realm
                .connect_to_protocol(COUNTER_B_PROTOCOL_NAME, None, server_end.into_channel())
                .expect("failed to connect to CounterB protocol");
            counter_b
        };
        assert_eq!(
            counter_b
                .increment()
                .await
                .expect("fuchsia.netemul.test/CounterB.increment call failed"),
            2,
        );
        // TODO(https://fxbug.dev/65359): once we can allow the ERROR logs that result from the
        // routing failure, verify that counter-b does *not* have access to counter-a's protocol.
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
            .expect("create directory proxy");
        let () = realm.get_devfs(server).expect("calling get devfs");
        fvfs_watcher::Watcher::new(devfs).await.expect("watcher creation")
    }

    async fn wait_for_event_on_path(
        watcher: &mut fvfs_watcher::Watcher,
        event: fvfs_watcher::WatchEvent,
        path: &std::path::Path,
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
                &std::path::Path::new(&name),
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
            let (controller, server_end) =
                fidl::endpoints::create_proxy::<fdevice::ControllerMarker>()
                    .expect("failed to create proxy");
            let () = get_device_proxy(&endpoint)
                .into_proxy()
                .expect("failed to create device proxy from client end")
                .serve_device(server_end.into_channel())
                .expect("failed to serve device");
            let path = controller
                .get_topological_path()
                .await
                .expect("calling get topological path")
                .map_err(zx::Status::from_raw)
                .expect("failed to get topological path");
            assert!(path.contains(&name));

            let () = realm
                .remove_device(&name)
                .await
                .expect("calling remove device")
                .map_err(zx::Status::from_raw)
                .expect("error removing device");
            let () = wait_for_event_on_path(
                &mut watcher,
                fvfs_watcher::WatchEvent::REMOVE_FILE,
                &std::path::Path::new(&name),
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
            &std::path::Path::new(TEST_DEVICE_NAME),
        )
        .await;
        // Expect not to see a matching device in `realm_b`'s devfs.
        let devfs_b = {
            let (devfs, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
                .expect("create directory proxy");
            let () = realm_b.get_devfs(server).expect("calling get devfs");
            devfs
        };
        let (status, mut buf) =
            devfs_b.read_dirents(fio::MAX_BUF).await.expect("calling read dirents");
        let () = zx::Status::ok(status).expect("failed reading directory entries");
        assert_eq!(
            fuchsia_fs::directory::parse_dir_entries(&mut buf)
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("failed parsing directory entries"),
            &[fuchsia_fs::directory::DirEntry {
                name: ".".to_string(),
                kind: fuchsia_fs::directory::DirentKind::Directory
            }],
        );
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn devfs_used_by_child(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![
                    fnetemul::ChildDef {
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        name: Some("counter-with-devfs".to_string()),
                        exposes: Some(vec![CounterMarker::PROTOCOL_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                            fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                                name: Some("dev".to_string()),
                                subdir: None,
                                ..fnetemul::DevfsDep::EMPTY
                            }),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    // TODO(https://fxbug.dev/65359): when we can allow ERROR logs for routing
                    // errors, add a child component that does not `use` `devfs`, and verify that we
                    // cannot get at the realm's `devfs` through it. It should result in a
                    // zx::Status::UNAVAILABLE error.
                ]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();

        let backings = [
            fnetemul_network::EndpointBacking::Ethertap,
            fnetemul_network::EndpointBacking::NetworkDevice,
        ];
        for (i, backing) in backings.iter().enumerate() {
            let class = match backing {
                fnetemul_network::EndpointBacking::Ethertap => "ethernet",
                fnetemul_network::EndpointBacking::NetworkDevice => "network",
            };
            let name = format!("{}_test_{}", class, i);
            let endpoint = create_endpoint(
                &sandbox,
                &name,
                fnetemul_network::EndpointConfig { mtu: 1500, mac: None, backing: *backing },
            )
            .await;
            let () = realm
                .realm
                .add_device(&name, get_device_proxy(&endpoint))
                .await
                .expect("FIDL error")
                .map_err(zx::Status::from_raw)
                .expect("error adding device");

            // Expect the device to implement `fuchsia.device/Controller.GetTopologicalPath`.
            let (controller, server_end) =
                fidl::endpoints::create_proxy::<fdevice::ControllerMarker>()
                    .expect("failed to create proxy");
            let () = counter
                .open_in_namespace(
                    &format!("{}/{}", DEVFS_PATH, name),
                    // TODO(https://fxbug.dev/77059): remove write permissions once they are no
                    // longer required to connect to protocols.
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    server_end.into_channel(),
                )
                .expect("failed to connect to device through counter");
            let path = controller
                .get_topological_path()
                .await
                .expect("FIDL error")
                .map_err(zx::Status::from_raw)
                .expect("failed to get topological path");
            assert!(path.contains(&name));
        }
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn storage_used_by_child(sandbox: fnetemul::SandboxProxy) {
        fn connect_to_counter(
            realm: &fnetemul::ManagedRealmProxy,
            name: &str,
        ) -> fnetemul_test::CounterProxy {
            let (counter, server_end) = fidl::endpoints::create_proxy::<CounterMarker>()
                .expect("failed to create counter proxy");
            let () = realm
                .connect_to_protocol(name, None, server_end.into_channel())
                .expect("failed to connect to counter protocol");
            counter
        }
        const COUNTER_WITH_STORAGE: &str = "counter-with-storage";
        const COUNTER_WITHOUT_STORAGE: &str = "counter-without-storage";
        let TestRealm { realm } = TestRealm::new(
            &sandbox,
            RealmOptions {
                children: Some(vec![
                    fnetemul::ChildDef {
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        name: Some(COUNTER_WITH_STORAGE.to_string()),
                        exposes: Some(vec![COUNTER_A_PROTOCOL_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                            fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                                variant: Some(fnetemul::StorageVariant::Data),
                                path: Some(String::from(DATA_PATH)),
                                ..fnetemul::StorageDep::EMPTY
                            }),
                            fnetemul::Capability::StorageDep(fnetemul::StorageDep {
                                variant: Some(fnetemul::StorageVariant::Cache),
                                path: Some(String::from(CACHE_PATH)),
                                ..fnetemul::StorageDep::EMPTY
                            }),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                    fnetemul::ChildDef {
                        source: Some(fnetemul::ChildSource::Component(COUNTER_URL.to_string())),
                        name: Some(COUNTER_WITHOUT_STORAGE.to_string()),
                        exposes: Some(vec![COUNTER_B_PROTOCOL_NAME.to_string()]),
                        uses: Some(fnetemul::ChildUses::Capabilities(vec![
                            fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        ])),
                        ..fnetemul::ChildDef::EMPTY
                    },
                ]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter_storage = connect_to_counter(&realm, COUNTER_A_PROTOCOL_NAME);
        let counter_without_storage = connect_to_counter(&realm, COUNTER_B_PROTOCOL_NAME);

        for dir in [CACHE_PATH, DATA_PATH] {
            let () = counter_storage
                .try_open_directory(dir)
                .await
                .unwrap_or_else(|e| panic!("calling open {}: {:?}", dir, e))
                .map_err(zx::Status::from_raw)
                .unwrap_or_else(|e| panic!("failed to open {}: {:?}", dir, e));
            let result = counter_without_storage
                .try_open_directory(dir)
                .await
                .unwrap_or_else(|e| panic!("calling open {}: {:?}", dir, e))
                .map_err(zx::Status::from_raw);
            assert_eq!(result, Err(zx::Status::NOT_FOUND), "opening {}", dir);
        }
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn stop_child_component_stops_child(sandbox: fnetemul::SandboxProxy) {
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();
        assert_eq!(counter.increment().await.expect("failed to increment counter"), 1);
        let TestRealm { realm } = realm;
        let () = realm
            .stop_child_component(COUNTER_COMPONENT_NAME)
            .await
            .expect("calling stop child component")
            .map_err(zx::Status::from_raw)
            .expect("stop child component failed");
        let err =
            counter.increment().await.expect_err("increment call on stopped child should fail");
        assert_matches::assert_matches!(
            err,
            fidl::Error::ClientChannelClosed { status, protocol_name }
                if status == zx::Status::PEER_CLOSED &&
                    protocol_name == CounterMarker::PROTOCOL_NAME
        );
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn stop_child_component_without_child(sandbox: fnetemul::SandboxProxy) {
        let TestRealm { realm } =
            TestRealm::new(&sandbox, fnetemul::RealmOptions { ..fnetemul::RealmOptions::EMPTY });
        let err = realm
            .stop_child_component(COUNTER_COMPONENT_NAME)
            .await
            .expect("calling stop child component")
            .map_err(zx::Status::from_raw)
            .expect_err("stop child component without child should fail");
        assert_eq!(err, zx::Status::NOT_FOUND);
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn stop_child_component_with_invalid_component_name(sandbox: fnetemul::SandboxProxy) {
        let TestRealm { realm } =
            TestRealm::new(&sandbox, fnetemul::RealmOptions { ..fnetemul::RealmOptions::EMPTY });
        let err = realm
            .stop_child_component("com/.\\/\\.ponent")
            .await
            .expect("calling stop child component")
            .map_err(zx::Status::from_raw)
            .expect_err("stop child component with invalid component name should fail");
        assert_eq!(err, zx::Status::INVALID_ARGS);
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn devfs_intermediate_directories(sandbox: fnetemul::SandboxProxy) {
        let TestRealm { realm } = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![counter_component()]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        const CLASS_DIR: &str = "class";
        const ETHERNET_DIR: &str = "ethernet";
        const TEST_DEVICE_NAME: &str = "ep0";
        let ethernet_path = format!("{}/{}", CLASS_DIR, ETHERNET_DIR);
        let test_device_path = format!("{}/{}/{}", CLASS_DIR, ETHERNET_DIR, TEST_DEVICE_NAME);
        let endpoint = create_endpoint(
            &sandbox,
            TEST_DEVICE_NAME,
            fnetemul_network::EndpointConfig {
                mtu: 1500,
                mac: None,
                backing: fnetemul_network::EndpointBacking::Ethertap,
            },
        )
        .await;

        let (devfs, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");
        let () = realm.get_devfs(server_end).expect("calling get devfs");
        let mut dev_watcher =
            fvfs_watcher::Watcher::new(Clone::clone(&devfs)).await.expect("watcher creation");
        let () = realm
            .add_device(&test_device_path, get_device_proxy(&endpoint))
            .await
            .expect("calling add device")
            .map_err(zx::Status::from_raw)
            .expect("error adding device");
        let () = wait_for_event_on_path(
            &mut dev_watcher,
            fvfs_watcher::WatchEvent::ADD_FILE,
            &std::path::Path::new(CLASS_DIR),
        )
        .await;

        let (ethernet, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");
        let () = devfs
            .open(
                fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_DIRECTORY,
                &ethernet_path,
                server_end.into_channel().into(),
            )
            .expect("calling open");
        let mut ethernet_watcher =
            fvfs_watcher::Watcher::new(ethernet).await.expect("watcher creation");
        let () = wait_for_event_on_path(
            &mut ethernet_watcher,
            fvfs_watcher::WatchEvent::EXISTING,
            &std::path::Path::new(TEST_DEVICE_NAME),
        )
        .await;
        let () = realm
            .remove_device(&test_device_path)
            .await
            .expect("calling remove device")
            .map_err(zx::Status::from_raw)
            .expect("error removing device");
        let () = wait_for_event_on_path(
            &mut ethernet_watcher,
            fvfs_watcher::WatchEvent::REMOVE_FILE,
            &std::path::Path::new(TEST_DEVICE_NAME),
        )
        .await;
    }

    #[fixture(with_sandbox)]
    // TODO(https://fxbug.dev/65359): when we can allowlist particular ERROR logs in a test, we can
    // use #[fuchsia::test] which initializes syslog.
    #[fasync::run_singlethreaded(test)]
    async fn add_remove_device_invalid_path(sandbox: fnetemul::SandboxProxy) {
        let TestRealm { realm } = TestRealm::new(&sandbox, fnetemul::RealmOptions::EMPTY);
        const INVALID_FILE_PATH: &str = "class/ethernet/..";
        let (device_proxy, _server) =
            fidl::endpoints::create_endpoints::<fnetemul_network::DeviceProxy_Marker>()
                .expect("create device proxy endpoints");
        let err = realm
            .add_device(INVALID_FILE_PATH, device_proxy)
            .await
            .expect("calling add device")
            .map_err(zx::Status::from_raw)
            .expect_err("add device with invalid path should fail");
        assert_eq!(err, zx::Status::INVALID_ARGS);
        let err = realm
            .remove_device(INVALID_FILE_PATH)
            .await
            .expect("calling remove device")
            .map_err(zx::Status::from_raw)
            .expect_err("remove device with invalid path should fail");
        assert_eq!(err, zx::Status::INVALID_ARGS);
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn devfs_subdirs_created_on_request(sandbox: fnetemul::SandboxProxy) {
        const DEVFS_SUBDIR_USER_URL: &str = "#meta/devfs-subdir-user.cm";
        const SUBDIR: &str = "class/ethernet";
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![fnetemul::ChildDef {
                    source: Some(fnetemul::ChildSource::Component(
                        DEVFS_SUBDIR_USER_URL.to_string(),
                    )),
                    name: Some(COUNTER_COMPONENT_NAME.to_string()),
                    exposes: Some(vec![CounterMarker::PROTOCOL_NAME.to_string()]),
                    uses: Some(fnetemul::ChildUses::Capabilities(vec![
                        fnetemul::Capability::LogSink(fnetemul::Empty {}),
                        fnetemul::Capability::NetemulDevfs(fnetemul::DevfsDep {
                            name: Some("dev-class-ethernet".to_string()),
                            subdir: Some(SUBDIR.to_string()),
                            ..fnetemul::DevfsDep::EMPTY
                        }),
                    ])),
                    ..fnetemul::ChildDef::EMPTY
                }]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();
        let path = format!("{}/{}", DEVFS_PATH, SUBDIR);

        let (ethernet, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("create directory proxy");
        let () = counter
            .open_in_namespace(&path, fio::OpenFlags::RIGHT_READABLE, server_end.into_channel())
            .expect(&format!("failed to connect to {} through counter", path));
        let (status, mut buf) =
            ethernet.read_dirents(fio::MAX_BUF).await.expect("calling read dirents");
        let () = zx::Status::ok(status).expect("failed reading directory entries");
        assert_eq!(
            fuchsia_fs::directory::parse_dir_entries(&mut buf)
                .into_iter()
                .collect::<Result<Vec<_>, _>>()
                .expect("failed parsing directory entries"),
            &[fuchsia_fs::directory::DirEntry {
                name: ".".to_string(),
                kind: fuchsia_fs::directory::DirentKind::Directory
            }],
        );
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn override_program_args(sandbox: fnetemul::SandboxProxy) {
        const STARTING_VALUE: u32 = 9000;
        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![fnetemul::ChildDef {
                    program_args: Some(vec![
                        "--starting-value".to_string(),
                        STARTING_VALUE.to_string(),
                    ]),
                    ..counter_component()
                }]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();
        assert_eq!(
            counter.increment().await.expect("failed to increment counter"),
            STARTING_VALUE + 1,
        );
    }

    #[fixture(with_sandbox)]
    #[fuchsia::test]
    async fn mock_child(sandbox: fnetemul::SandboxProxy) {
        let (mock_dir, server_end) = fidl::endpoints::create_endpoints().expect("create endpoints");

        let mut fs = ServiceFs::new();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(|s: fnetemul_test::CounterRequestStream| s);

        let _: &mut ServiceFs<_> = fs.serve_connection(server_end).expect("serve connection");

        let realm = TestRealm::new(
            &sandbox,
            fnetemul::RealmOptions {
                children: Some(vec![fnetemul::ChildDef {
                    source: Some(fnetemul::ChildSource::Mock(mock_dir)),
                    ..counter_component()
                }]),
                ..fnetemul::RealmOptions::EMPTY
            },
        );
        let counter = realm.connect_to_protocol::<CounterMarker>();
        let counter_fut = counter.increment();

        let counter_request = fs
            .flatten()
            .try_next()
            .await
            .expect("next request")
            .expect("service fs ended unexpectedly");

        const RESPONSE_VALUE: u32 = 1234;
        match counter_request {
            fnetemul_test::CounterRequest::Increment { responder } => {
                let () = responder.send(RESPONSE_VALUE).expect("failed to send response");
            }
            r => panic!("unexpected request {:?}", r),
        }

        assert_eq!(counter_fut.await.expect("increment failed"), RESPONSE_VALUE);
    }
}
