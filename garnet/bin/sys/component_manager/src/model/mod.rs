// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod moniker;
mod resolver;
mod runner;

pub use self::{moniker::*, resolver::*, runner::*};
use {
    crate::ns_util::PKG_PATH,
    crate::{data, directory_broker, io_util},
    cm_rust::{self, CapabilityPath, ComponentDecl, RelativeId, UseDecl},
    failure::{format_err, Error, Fail},
    fidl::endpoints::{create_endpoints, ClientEnd, Proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryProxy, NodeMarker, MODE_TYPE_DIRECTORY, MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE,
    },
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_vfs_pseudo_fs as fvfs,
    fuchsia_vfs_pseudo_fs::directory::entry::DirectoryEntry,
    fuchsia_zircon as zx,
    futures::future::{AbortHandle, Abortable, FutureObj},
    futures::lock::Mutex,
    std::convert::TryInto,
    std::{collections::HashMap, iter, sync::Arc},
};

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// The URI of the root component.
    pub root_component_uri: String,
    /// The component resolver registry used in the root realm.
    /// In particular, it will be used to resolve the root component itself.
    pub root_resolver_registry: ResolverRegistry,
    /// The default runner used in the root realm (nominally runs ELF binaries).
    pub root_default_runner: Box<dyn Runner + Send + Sync + 'static>,
}

/// The component model holds authoritative state about a tree of component instances, including
/// each instance's identity, lifecycle, capabilities, and topological relationships.  It also
/// provides operations for instantiating, destroying, querying, and controlling component
/// instances at runtime.
///
/// To facilitate unit testing, the component model does not directly perform IPC.  Instead, it
/// delegates external interfacing concerns to other objects that implement traits such as
/// `Runner` and `Resolver`.
#[derive(Clone)]
pub struct Model {
    root_realm: Arc<Mutex<Realm>>,
}

/// A realm is a container for an individual component instance and its children.  It is provided
/// by the parent of the instance or by the component manager itself in the case of the root realm.
///
/// The realm's properties influence the runtime behavior of the subtree of component instances
/// that it contains, including component resolution, execution, and service discovery.
type ChildRealmMap = HashMap<ChildMoniker, Arc<Mutex<Realm>>>;
struct Realm {
    /// The registry for resolving component URIs within the realm.
    resolver_registry: Arc<ResolverRegistry>,
    /// The default runner (nominally runs ELF binaries) for executing components
    /// within the realm that do not explicitly specify a runner.
    default_runner: Arc<Box<dyn Runner + Send + Sync + 'static>>,
    /// The component that has been instantiated within the realm.
    instance: Instance,
    /// The absolute moniker of this realm.
    abs_moniker: AbsoluteMoniker,
}

/// An instance of a component.
struct Instance {
    /// The component's URI.
    component_uri: String,
    /// Execution state for the component instance or `None` if not running.
    execution: Mutex<Option<Execution>>,
    /// Realms of child instances, indexed by child moniker (name). Evaluated on demand.
    child_realms: Option<ChildRealmMap>,
    /// The component's validated declaration. Evaluated on demand.
    decl: Option<ComponentDecl>,
    /// The mode of startup (lazy or eager).
    startup: fsys::StartupMode,
}

impl Instance {
    fn make_child_realms(
        component: &fsys::Component,
        abs_moniker: &AbsoluteMoniker,
        resolver_registry: Arc<ResolverRegistry>,
        default_runner: Arc<Box<dyn Runner + Send + Sync + 'static>>,
    ) -> Result<ChildRealmMap, ModelError> {
        let mut child_realms = HashMap::new();
        if component.decl.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        if let Some(ref children_decl) = component.decl.as_ref().unwrap().children {
            for child_decl in children_decl {
                let child_name = child_decl.name.as_ref().unwrap().clone();
                let child_uri = child_decl.uri.as_ref().unwrap().clone();
                let moniker = ChildMoniker::new(child_name);
                let abs_moniker = abs_moniker.child(moniker.clone());
                let realm = Arc::new(Mutex::new(Realm {
                    resolver_registry: resolver_registry.clone(),
                    default_runner: default_runner.clone(),
                    abs_moniker: abs_moniker,
                    instance: Instance {
                        component_uri: child_uri,
                        execution: Mutex::new(None),
                        child_realms: None,
                        decl: None,
                        startup: child_decl.startup.unwrap(),
                    },
                }));
                child_realms.insert(moniker, realm);
            }
        }
        Ok(child_realms)
    }
}

/// The execution state for a component instance that has started running.
// TODO: Hold the component instance's controller.
struct Execution {
    resolved_uri: String,
    package_dir: Option<DirectoryProxy>,
    outgoing_dir: DirectoryProxy,
    dir_abort_handles: Vec<AbortHandle>,
}

impl Drop for Execution {
    fn drop(&mut self) {
        for abort_handle in &self.dir_abort_handles {
            abort_handle.abort();
        }
    }
}

impl Execution {
    fn start_from(
        resolved_uri: Option<String>,
        package: Option<fsys::Package>,
        outgoing_dir: DirectoryProxy,
    ) -> Result<Self, ModelError> {
        if resolved_uri.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        let package_dir = match package {
            Some(package) => {
                if package.package_dir.is_none() {
                    return Err(ModelError::ComponentInvalid);
                }
                let package_dir = package
                    .package_dir
                    .unwrap()
                    .into_proxy()
                    .expect("could not convert package dir to proxy");
                Some(package_dir)
            }
            None => None,
        };
        let uri = resolved_uri.unwrap();
        Ok(Execution { resolved_uri: uri, package_dir, outgoing_dir, dir_abort_handles: vec![] })
    }
}

impl Execution {
    /// add_directory_use will install one end of a channel pair in the namespace under the
    /// target_path, and will add a future to waiters that will wait on the other end of a channel
    /// for a signal. Once the channel is readable, the future calls model.route_directory and
    /// terminates.
    fn add_directory_use(
        ns: &mut fsys::ComponentNamespace,
        waiters: &mut Vec<FutureObj<()>>,
        use_: &UseDecl,
        model: Model,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let (client_end, server_end) =
            create_endpoints().expect("could not create directory proxy endpoints");
        let source_path = use_.source_path.clone();
        let route_on_usage = async move {
            // Wait for the channel to become readable.
            let server_end_chan = fasync::Channel::from_channel(server_end.into_channel())
                .expect("failed to convert server_end into async channel");
            let on_signal_fut =
                fasync::OnSignals::new(&server_end_chan, zx::Signals::CHANNEL_READABLE);
            await!(on_signal_fut).unwrap();
            // Route this capability to the right component
            await!(model.route_directory(
                source_path.clone(),
                abs_moniker,
                server_end_chan.into_zx_channel()
            ))
            .expect("failed to route directory");
        };

        waiters.push(FutureObj::new(Box::new(route_on_usage)));
        ns.paths.push(use_.target_path.to_string());
        ns.directories.push(client_end);
        Ok(())
    }

    /// start_directory_waiters will spawn the futures in directory_waiters as abortables, and adds
    /// the abort handles to the Execution.
    fn start_directory_waiters(
        &mut self,
        directory_waiters: Vec<FutureObj<'static, ()>>,
    ) -> Result<(), ModelError> {
        for waiter in directory_waiters {
            let (abort_handle, abort_registration) = AbortHandle::new_pair();
            self.dir_abort_handles.push(abort_handle);
            let future = Abortable::new(waiter, abort_registration);

            // The future for a directory waiter will only terminate once the directory channel is
            // first used, so we must start up a new task here to run the future instead of calling
            // await on it directly. This is wrapped in an async move {await!();}` block to drop
            // the unused return value.
            fasync::spawn(
                async move {
                    let _ = await!(future);
                },
            );
        }
        Ok(())
    }

    /// add_service_use will open the parent directory of source_path in componentmgr's namespace,
    /// create a DirectoryBroker to proxy requests from target_path, and add the broker under a
    /// pseudo directory in svc_dirs, creating a new pseudo directory if necessary.
    fn add_service_use(
        svc_dirs: &mut HashMap<String, fvfs::directory::simple::Simple>,
        use_: &UseDecl,
        model: Model,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let source_path = use_.source_path.clone();
        let route_service_fn = Box::new(move |server_end: ServerEnd<NodeMarker>| {
            let source_path = source_path.clone();
            let model = model.clone();
            let abs_moniker = abs_moniker.clone();
            fasync::spawn(
                async move {
                    await!(model.route_service(
                        source_path,
                        abs_moniker,
                        server_end.into_channel()
                    ))
                    .expect("failed to route service");
                },
            );
        });

        let service_dir = svc_dirs
            .entry(use_.target_path.dirname.clone())
            .or_insert(fvfs::directory::simple::empty());
        service_dir
            .add_entry(
                &use_.target_path.basename,
                directory_broker::DirectoryBroker::new_service_broker(route_service_fn),
            )
            .map_err(|(status, _)| status)
            .expect("could not add service to directory");
        Ok(())
    }

    /// serve_and_install_svc_dirs will take all of the pseudo directories collected in
    /// svc_dirs (as populated by add_service_use calls), start them as abortable futures, and
    /// install them in the namespace. The abortable handles are saved in the Execution, to be
    /// called when the Execution is dropped.
    fn serve_and_install_svc_dirs(
        &mut self,
        ns: &mut fsys::ComponentNamespace,
        svc_dirs: HashMap<String, fvfs::directory::simple::Simple<'static>>,
    ) -> Result<(), ModelError> {
        for (target_dir_path, mut pseudo_dir) in svc_dirs {
            let (client_end, server_end) =
                create_endpoints::<NodeMarker>().expect("could not create node proxy endpoints");
            pseudo_dir
                .open(OPEN_RIGHT_READABLE, MODE_TYPE_DIRECTORY, &mut iter::empty(), server_end)
                .expect("failed to open services dir");
            let (abort_handle, abort_registration) = AbortHandle::new_pair();
            self.dir_abort_handles.push(abort_handle);
            let future = Abortable::new(pseudo_dir, abort_registration);

            // The future for a pseudo directory will never terminate, so we must start up a new
            // task here to run the future instead of calling await on it directly. This is
            // wrapped in an async move {await!();}` block like to drop the unused return value.
            fasync::spawn(
                async move {
                    let _ = await!(future);
                },
            );

            ns.paths.push(target_dir_path.as_str().to_string());
            let client_end = ClientEnd::new(client_end.into_channel()); // coerce to ClientEnd<Dir>
            ns.directories.push(client_end);
        }
        Ok(())
    }

    /// add_pkg_directory will add a handle to the component's package under /pkg in the namespace.
    fn add_pkg_directory(
        ns: &mut fsys::ComponentNamespace,
        package_dir: &DirectoryProxy,
    ) -> Result<(), ModelError> {
        let clone_dir_proxy = io_util::clone_directory(package_dir)
            .map_err(|e| ModelError::namespace_creation_failed(e))?;
        let cloned_dir = ClientEnd::new(
            clone_dir_proxy
                .into_channel()
                .expect("could not convert directory to channel")
                .into_zx_channel(),
        );
        ns.paths.push(PKG_PATH.to_str().unwrap().to_string());
        ns.directories.push(cloned_dir);
        Ok(())
    }

    /// make_namespace will convert decl.uses into an inflated fsys::ComponentNamespace,
    /// serving and installing handles to pseudo directories.
    async fn make_namespace<'a>(
        &'a mut self,
        model: Model,
        abs_moniker: AbsoluteMoniker,
        decl: &'a ComponentDecl,
    ) -> Result<fsys::ComponentNamespace, ModelError> {
        let mut ns = fsys::ComponentNamespace { paths: vec![], directories: vec![] };

        // Populate the namespace from uses, using the component manager's namespace.
        // svc_dirs will hold (path,directory) pairs. Each pair holds a path in the
        // component's namespace and a directory that ComponentMgr will host for the component.
        let mut svc_dirs = HashMap::new();

        // directory_waiters will hold Future<Output=()> objects that will wait for activity on
        // a channel and then route the channel to the appropriate component's out directory.
        let mut directory_waiters = Vec::new();

        for use_ in &decl.uses {
            match use_.type_ {
                fsys::CapabilityType::Directory => {
                    Execution::add_directory_use(
                        &mut ns,
                        &mut directory_waiters,
                        &use_,
                        model.clone(),
                        abs_moniker.clone(),
                    )?;
                }
                fsys::CapabilityType::Service => {
                    Execution::add_service_use(
                        &mut svc_dirs,
                        &use_,
                        model.clone(),
                        abs_moniker.clone(),
                    )?;
                }
            }
        }

        // Start hosting the services directories and add them to the namespace
        self.serve_and_install_svc_dirs(&mut ns, svc_dirs)?;
        self.start_directory_waiters(directory_waiters)?;

        // Populate the /pkg namespace.
        if let Some(package_dir) = self.package_dir.as_ref() {
            Execution::add_pkg_directory(&mut ns, package_dir)?;
        }
        Ok(ns)
    }
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            root_realm: Arc::new(Mutex::new(Realm {
                resolver_registry: Arc::new(params.root_resolver_registry),
                default_runner: Arc::new(params.root_default_runner),
                abs_moniker: AbsoluteMoniker::root(),
                instance: Instance {
                    component_uri: params.root_component_uri,
                    execution: Mutex::new(None),
                    child_realms: None,
                    decl: None,
                    // Started by main().
                    startup: fsys::StartupMode::Lazy,
                },
            })),
        }
    }

    /// Binds to the component instance with the specified moniker, causing it to start if it is
    /// not already running. Also binds to any descendant component instances that need to be
    /// eagerly started.
    pub async fn bind_instance(&self, abs_moniker: AbsoluteMoniker) -> Result<(), ModelError> {
        let realm = await!(self.look_up_realm(&abs_moniker))?;
        // We may have to bind to multiple instances if this instance has children with the
        // "eager" startup mode.
        let mut instances_to_bind = vec![realm];
        while let Some(realm) = instances_to_bind.pop() {
            instances_to_bind.append(&mut await!(self.bind_instance_in_realm(realm))?);
        }
        Ok(())
    }

    /// Binds to the component instance in the given realm, starting it if it's not
    /// already running. Returns the list of child realms whose instances need to be eagerly started
    /// after this function returns.
    async fn bind_instance_in_realm(
        &self,
        realm_cell: Arc<Mutex<Realm>>,
    ) -> Result<Vec<Arc<Mutex<Realm>>>, ModelError> {
        // There can only be one task manipulating an instance's execution at a time.
        let Realm { ref resolver_registry, ref default_runner, ref abs_moniker, ref mut instance } =
            *await!(realm_cell.lock());
        let Instance {
            ref component_uri,
            ref execution,
            ref mut child_realms,
            ref mut decl,
            startup: _,
        } = instance;
        let mut execution_lock = await!(execution.lock());
        let mut started = false;
        match &*execution_lock {
            Some(_) => {}
            None => {
                let component = await!(resolver_registry.resolve(component_uri))?;
                // TODO: the following logic that populates some fields in Instance is duplicated
                // in look_up_in_realm. Find a way to prevent that duplication.
                if child_realms.is_none() {
                    *child_realms = Some(Instance::make_child_realms(
                        &component,
                        abs_moniker,
                        resolver_registry.clone(),
                        default_runner.clone(),
                    )?);
                }
                if decl.is_none() {
                    *decl =
                        Some(component.decl.unwrap().try_into().map_err(|e| {
                            ModelError::manifest_invalid(component_uri.to_string(), e)
                        })?)
                }
                let decl = instance.decl.as_ref().unwrap();
                // TODO(CF-647): Serve in the Instance's PseudoDir instead.
                let (outgoing_dir_client, outgoing_dir_server) =
                    zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
                let mut execution = Execution::start_from(
                    component.resolved_uri,
                    component.package,
                    DirectoryProxy::from_channel(
                        fasync::Channel::from_channel(outgoing_dir_client).unwrap(),
                    ),
                )?;

                let ns =
                    await!(execution.make_namespace(self.clone(), abs_moniker.clone(), decl,))?;

                let start_info = fsys::ComponentStartInfo {
                    resolved_uri: Some(execution.resolved_uri.clone()),
                    program: data::clone_option_dictionary(&decl.program),
                    ns: Some(ns),
                    outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
                };
                await!(default_runner.start(start_info))?;
                started = true;
                *execution_lock = Some(execution);
            }
        }
        // Return children that need eager starting.
        let mut eager_children = vec![];
        if started {
            for child_realm in instance.child_realms.as_ref().unwrap().values() {
                let startup = await!(child_realm.lock()).instance.startup;
                match startup {
                    fsys::StartupMode::Eager => {
                        eager_children.push(child_realm.clone());
                    }
                    fsys::StartupMode::Lazy => {}
                }
            }
        }
        Ok(eager_children)
    }

    async fn look_up_realm<'a>(
        &'a self,
        look_up_abs_moniker: &'a AbsoluteMoniker,
    ) -> Result<Arc<Mutex<Realm>>, ModelError> {
        let mut cur_realm = self.root_realm.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur_realm = {
                let Realm {
                    ref resolver_registry,
                    ref default_runner,
                    ref abs_moniker,
                    ref mut instance,
                } = *await!(cur_realm.lock());
                let mut component = None;
                if instance.child_realms.is_none() {
                    component = Some(await!(resolver_registry.resolve(&instance.component_uri))?);
                    instance.child_realms = Some(Instance::make_child_realms(
                        component.as_ref().unwrap(),
                        abs_moniker,
                        resolver_registry.clone(),
                        default_runner.clone(),
                    )?);
                }
                if instance.decl.is_none() {
                    if component.is_none() {
                        component =
                            Some(await!(resolver_registry.resolve(&instance.component_uri))?);
                    }
                    instance.decl =
                        Some(component.unwrap().decl.unwrap().try_into().map_err(|e| {
                            ModelError::manifest_invalid(look_up_abs_moniker.to_string(), e)
                        })?)
                }
                let child_realms = instance.child_realms.as_ref().unwrap();
                if !child_realms.contains_key(&moniker) {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
                child_realms[moniker].clone()
            }
        }
        Ok(cur_realm)
    }

    /// `route_directory` will find the source of the directory capability used at `source_path` by
    /// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
    /// directory (or componentmgr's namespace, if applicable)
    async fn route_directory(
        &self,
        source_path: CapabilityPath,
        abs_moniker: AbsoluteMoniker,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        await!(self.route_capability(
            fsys::CapabilityType::Directory,
            MODE_TYPE_DIRECTORY,
            source_path,
            abs_moniker,
            server_chan
        ))
    }

    /// `route_service` will find the source of the service capability used at `source_path` by
    /// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
    /// directory (or componentmgr's namespace, if applicable)
    async fn route_service(
        &self,
        source_path: CapabilityPath,
        abs_moniker: AbsoluteMoniker,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        await!(self.route_capability(
            fsys::CapabilityType::Service,
            MODE_TYPE_SERVICE,
            source_path,
            abs_moniker,
            server_chan
        ))
    }

    /// `route_capability` will find the source of the capability `type_` used at `source_path` by
    /// `absolute_moniker`, and pass along the `server_chan` to the hosting component's out
    /// directory (or componentmgr's namespace, if applicable) using an open request with
    /// `open_mode`.
    async fn route_capability(
        &self,
        type_: fsys::CapabilityType,
        open_mode: u32,
        source_path: CapabilityPath,
        abs_moniker: AbsoluteMoniker,
        server_chan: zx::Channel,
    ) -> Result<(), ModelError> {
        let source = await!(self.find_capability_source(type_, source_path, abs_moniker))?;

        let flags = OPEN_RIGHT_READABLE;
        match source {
            CapabilitySource::ComponentMgrNamespace(path) => {
                io_util::connect_in_namespace(&path.to_string(), server_chan)
                    .map_err(|e| ModelError::capability_discovery_error(e))?
            }
            CapabilitySource::Component(path, realm) => {
                let server_end = ServerEnd::new(server_chan);
                let realm = await!(realm.lock());
                let execution = await!(realm.instance.execution.lock());
                let out_dir = &execution
                    .as_ref()
                    .ok_or(ModelError::capability_discovery_error(format_err!(
                        "component hosting capability isn't running: {}",
                        realm.abs_moniker
                    )))?
                    .outgoing_dir;

                let path = io_util::canonicalize_path(&path.to_string());

                out_dir
                    .open(flags, open_mode, &path, server_end)
                    .expect("failed to send open message");
            }
        }

        Ok(())
    }

    /// find_capability_source will walk the component tree to find the originating source of a
    /// capability, starting on the given abs_moniker and source_path. It returns the absolute
    /// moniker of the originating component, a reference to its realm, and the path that the
    /// component is exposing the capability from. If the absolute moniker and realm are None, then
    /// the capability originates at the returned path in componentmgr's namespace.
    async fn find_capability_source(
        &self,
        type_: fsys::CapabilityType,
        source_path: CapabilityPath,
        abs_moniker: AbsoluteMoniker,
    ) -> Result<CapabilitySource, ModelError> {
        // Holds mutable state as we walk the tree
        struct State {
            // The current path of the capability
            path: CapabilityPath,
            // The name of the child we came from
            name: Option<ChildMoniker>,
            // The moniker of the component we are currently looking at
            moniker: AbsoluteMoniker,
        }
        let moniker = match abs_moniker.parent() {
            Some(m) => m,
            None => return Ok(CapabilitySource::ComponentMgrNamespace(source_path)),
        };
        let mut s = State {
            path: source_path,
            name: abs_moniker.path().last().map(|c| c.clone()),
            moniker: moniker,
        };
        // Walk offer chain
        'offerloop: loop {
            let current_realm_mutex = await!(self.look_up_realm(&s.moniker))?;
            let current_realm = await!(current_realm_mutex.lock());
            // This unwrap is safe because look_up_realm populates this field
            let decl = current_realm.instance.decl.as_ref().unwrap();

            if let Some(offer) = decl.find_offer_source(&s.path, &type_, &s.name.unwrap().name()) {
                match &offer.source {
                    RelativeId::Realm => {
                        // The offered capability comes from the realm, so follow the
                        // parent
                        s.path = offer.source_path.clone();
                        s.name = s.moniker.path().last().map(|c| c.clone());
                        s.moniker = match s.moniker.parent() {
                            Some(m) => m,
                            None => return Ok(CapabilitySource::ComponentMgrNamespace(s.path)),
                        };
                        continue 'offerloop;
                    }
                    RelativeId::Myself => {
                        // The offered capability comes from the current component,
                        // return our current location in the tree.
                        return Ok(CapabilitySource::Component(
                            offer.source_path.clone(),
                            current_realm_mutex.clone(),
                        ));
                    }
                    RelativeId::Child(child_name) => {
                        // The offered capability comes from a child, break the loop
                        // and begin walking the expose chain.
                        s.path = offer.source_path.clone();
                        s.moniker = s.moniker.child(ChildMoniker::new(child_name.to_string()));
                        break 'offerloop;
                    }
                }
            } else {
                return Err(ModelError::capability_discovery_error(format_err!(
                    "no matching offers found for capability {} from component {}",
                    s.path,
                    s.moniker
                )));
            }
        }
        // Walk expose chain
        loop {
            let current_realm_mutex = await!(self.look_up_realm(&s.moniker))?;
            let current_realm = await!(current_realm_mutex.lock());
            // This unwrap is safe because look_up_realm populates this field
            let decl = current_realm.instance.decl.as_ref().unwrap();

            if let Some(expose) = decl.find_expose_source(&s.path, &type_) {
                match &expose.source {
                    RelativeId::Myself => {
                        // The offered capability comes from the current component, return our
                        // current location in the tree.
                        return Ok(CapabilitySource::Component(
                            expose.source_path.clone(),
                            current_realm_mutex.clone(),
                        ));
                    }
                    RelativeId::Child(child_name) => {
                        // The offered capability comes from a child, so follow the child.
                        s.path = expose.source_path.clone();
                        s.moniker = s.moniker.child(ChildMoniker::new(child_name.to_string()));
                        continue;
                    }
                    _ => panic!("relation on an expose wasn't self or child"),
                }
            } else {
                // We didn't find any matching exposes! Oh no!
                return Err(ModelError::capability_discovery_error(format_err!(
                    "no matching exposes found for capability {} from component {}",
                    s.path,
                    s.moniker
                )));
            }
        }
    }
}

/// Describes the source of a capability, as determined by `find_capability_source`
enum CapabilitySource {
    /// This capability source comes from the component described by this AbsoluteMoniker at
    /// this path. The Realm is provided as well, as it has already been looked up by this
    /// point.
    Component(CapabilityPath, Arc<Mutex<Realm>>),
    /// This capability source comes from component manager's namespace, at this path
    ComponentMgrNamespace(CapabilityPath),
}

/// Errors produced by `Model`.
#[derive(Debug, Fail)]
pub enum ModelError {
    #[fail(display = "component instance not found with moniker {}", moniker)]
    InstanceNotFound { moniker: AbsoluteMoniker },
    #[fail(display = "component declaration invalid")]
    ComponentInvalid,
    #[fail(display = "component manifest invalid")]
    ManifestInvalid {
        uri: String,
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "namespace creation failed: {}", err)]
    NamespaceCreationFailed {
        #[fail(cause)]
        err: Error,
    },
    #[fail(display = "resolver error")]
    ResolverError {
        #[fail(cause)]
        err: ResolverError,
    },
    #[fail(display = "runner error")]
    RunnerError {
        #[fail(cause)]
        err: RunnerError,
    },
    #[fail(display = "capability discovery error")]
    CapabilityDiscoveryError {
        #[fail(cause)]
        err: Error,
    },
}

impl ModelError {
    fn instance_not_found(moniker: AbsoluteMoniker) -> ModelError {
        ModelError::InstanceNotFound { moniker }
    }

    fn namespace_creation_failed(err: impl Into<Error>) -> ModelError {
        ModelError::NamespaceCreationFailed { err: err.into() }
    }

    fn manifest_invalid(uri: impl Into<String>, err: impl Into<Error>) -> ModelError {
        ModelError::ManifestInvalid { uri: uri.into(), err: err.into() }
    }

    fn capability_discovery_error(err: impl Into<Error>) -> ModelError {
        ModelError::CapabilityDiscoveryError { err: err.into() }
    }
}

impl From<ResolverError> for ModelError {
    fn from(err: ResolverError) -> Self {
        ModelError::ResolverError { err }
    }
}

impl From<RunnerError> for ModelError {
    fn from(err: RunnerError) -> Self {
        ModelError::RunnerError { err }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fdio,
        fidl_fidl_examples_echo::{self as echo, EchoMarker, EchoRequest, EchoRequestStream},
        fidl_fuchsia_io::DirectoryMarker,
        fidl_fuchsia_io::MODE_TYPE_SERVICE,
        fuchsia_vfs_pseudo_fs::{file::simple::read_only, pseudo_directory},
        fuchsia_zircon::HandleBased,
        futures::future::FutureObj,
        futures::TryStreamExt,
        lazy_static::lazy_static,
        regex::Regex,
        std::collections::HashSet,
        std::ffi::CString,
        std::path::PathBuf,
        std::ptr,
    };

    struct ChildInfo {
        name: String,
        startup: fsys::StartupMode,
    }

    // TODO: impl MockResolver::new(), use it instead of always filling in these fields manually
    struct MockResolver {
        // Map of parent component instance to its children component instances.
        children: Arc<Mutex<HashMap<String, Vec<ChildInfo>>>>,
        // Map of component instance to vec of UseDecls of the component instance.
        uses: Arc<Mutex<HashMap<String, Option<Vec<fsys::UseDecl>>>>>,
        // Map of component instance to vec of OfferDecls of the component instance.
        offers: Arc<Mutex<HashMap<String, Option<Vec<fsys::OfferDecl>>>>>,
        // Map of component instance to vec of ExposeDecls of the component instance.
        exposes: Arc<Mutex<HashMap<String, Option<Vec<fsys::ExposeDecl>>>>>,
    }
    lazy_static! {
        static ref NAME_RE: Regex = Regex::new(r"test:///([0-9a-z\-\._]+)$").unwrap();
    }
    impl MockResolver {
        async fn resolve_async(
            &self,
            component_uri: String,
        ) -> Result<fsys::Component, ResolverError> {
            let caps = NAME_RE.captures(&component_uri).unwrap();
            let name = &caps[1];
            let children = await!(self.children.lock());
            let mut decl = new_component_decl();
            if let Some(children) = children.get(name) {
                decl.children = Some(
                    children
                        .iter()
                        .map(|c| fsys::ChildDecl {
                            name: Some(c.name.clone()),
                            uri: Some(format!("test:///{}", c.name)),
                            startup: Some(c.startup),
                        })
                        .collect(),
                );
            }
            let uses = await!(self.uses.lock());
            if let Some(uses) = uses.get(name) {
                // There's no clone function on this, so copy over the fields manually
                let mut decl_uses = vec![];
                for u in uses.as_ref().unwrap_or(&vec![]) {
                    decl_uses.push(fsys::UseDecl {
                        type_: u.type_.clone(),
                        source_path: u.source_path.clone(),
                        target_path: u.target_path.clone(),
                    });
                }
                decl.uses = Some(decl_uses);
            }
            let offers = await!(self.offers.lock());
            if let Some(offers) = offers.get(name) {
                // There's no clone function on this, so copy over the fields manually
                let mut decl_offers = vec![];
                for o in offers.as_ref().unwrap_or(&vec![]) {
                    let mut offer = fsys::OfferDecl {
                        type_: o.type_.clone(),
                        source_path: o.source_path.clone(),
                        source: Some(fsys::RelativeId {
                            relation: o.source.as_ref().unwrap().relation.clone(),
                            child_name: o.source.as_ref().unwrap().child_name.clone(),
                        }),
                        targets: None,
                    };
                    let mut offer_targets = vec![];
                    for t in o.targets.as_ref().unwrap_or(&vec![]) {
                        offer_targets.push(fsys::OfferTarget {
                            target_path: t.target_path.clone(),
                            child_name: t.child_name.clone(),
                        });
                    }
                    offer.targets = Some(offer_targets);
                    decl_offers.push(offer);
                }
                decl.offers = Some(decl_offers);
            }
            let exposes = await!(self.exposes.lock());
            if let Some(exposes) = exposes.get(name) {
                let mut decl_exposes = vec![];
                for e in exposes.as_ref().unwrap_or(&vec![]) {
                    decl_exposes.push(fsys::ExposeDecl {
                        type_: e.type_.clone(),
                        source_path: e.source_path.clone(),
                        source: Some(fsys::RelativeId {
                            relation: e.source.as_ref().unwrap().relation.clone(),
                            child_name: e.source.as_ref().unwrap().child_name.clone(),
                        }),
                        target_path: e.target_path.clone(),
                    });
                }
                decl.exposes = Some(decl_exposes);
            }
            Ok(fsys::Component {
                resolved_uri: Some(format!("test:///{}_resolved", name)),
                decl: Some(decl),
                package: None,
            })
        }
    }
    impl Resolver for MockResolver {
        fn resolve(
            &self,
            component_uri: &str,
        ) -> FutureObj<Result<fsys::Component, ResolverError>> {
            FutureObj::new(Box::new(self.resolve_async(component_uri.to_string())))
        }
    }
    struct MockRunner {
        uris_run: Arc<Mutex<Vec<String>>>,
        namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
        outgoing_dirs: Arc<Mutex<HashMap<String, ServerEnd<DirectoryMarker>>>>,
    }
    impl MockRunner {
        async fn start_async(
            &self,
            start_info: fsys::ComponentStartInfo,
        ) -> Result<(), RunnerError> {
            let resolved_uri = start_info.resolved_uri.unwrap();
            await!(self.uris_run.lock()).push(resolved_uri.clone());
            await!(self.namespaces.lock()).insert(resolved_uri.clone(), start_info.ns.unwrap());
            await!(self.outgoing_dirs.lock())
                .insert(resolved_uri.clone(), start_info.outgoing_dir.unwrap());
            Ok(())
        }
    }
    impl Runner for MockRunner {
        fn start(
            &self,
            start_info: fsys::ComponentStartInfo,
        ) -> FutureObj<Result<(), RunnerError>> {
            FutureObj::new(Box::new(self.start_async(start_info)))
        }
    }
    fn new_component_decl() -> fsys::ComponentDecl {
        fsys::ComponentDecl {
            program: None,
            uses: None,
            exposes: None,
            offers: None,
            facets: None,
            children: None,
        }
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_instance_root() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        let uses = Arc::new(Mutex::new(HashMap::new()));
        let offers = Arc::new(Mutex::new(HashMap::new()));
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        let res = await!(model.bind_instance(AbsoluteMoniker::root()));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris: Vec<String> = vec!["test:///root_resolved".to_string()];
        assert_eq!(*actual_uris, expected_uris);
        let root_realm = await!(model.root_realm.lock());
        let actual_children = get_children(&root_realm);
        assert!(actual_children.is_empty());
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_instance_root_non_existent() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        let uses = Arc::new(Mutex::new(HashMap::new()));
        let offers = Arc::new(Mutex::new(HashMap::new()));
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
            "no-such-instance".to_string()
        )])));
        let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(
            AbsoluteMoniker::new(vec![ChildMoniker::new("no-such-instance".to_string())]),
        ));
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        let actual_uris = await!(uris_run.lock());
        let expected_uris: Vec<String> = vec![];
        assert_eq!(*actual_uris, expected_uris);
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_instance_child() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "root".to_string(),
            vec![
                ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy },
                ChildInfo { name: "echo".to_string(), startup: fsys::StartupMode::Lazy },
            ],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));
        let offers = Arc::new(Mutex::new(HashMap::new()));
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        // bind to system
        {
            let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
                "system".to_string()
            ),])));
            let expected_res: Result<(), ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> = vec!["test:///system_resolved".to_string()];
            assert_eq!(*actual_uris, expected_uris);
        }
        // Validate children. system is resolved, but not echo.
        {
            let actual_children = get_children(&*await!(model.root_realm.lock()));
            let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
            expected_children.insert(ChildMoniker::new("system".to_string()));
            expected_children.insert(ChildMoniker::new("echo".to_string()));
            assert_eq!(actual_children, expected_children);
        }
        {
            let system_realm = get_child_realm(&*await!(model.root_realm.lock()), "system");
            let echo_realm = get_child_realm(&*await!(model.root_realm.lock()), "echo");
            let actual_children = get_children(&*await!(system_realm.lock()));
            assert!(actual_children.is_empty());
            assert!(await!(echo_realm.lock()).instance.child_realms.is_none());
        }
        // bind to echo
        {
            let res = await!(model
                .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("echo".to_string()),])));
            let expected_res: Result<(), ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> =
                vec!["test:///system_resolved".to_string(), "test:///echo_resolved".to_string()];
            assert_eq!(*actual_uris, expected_uris);
        }
        // Validate children. Now echo is resolved.
        {
            let echo_realm = get_child_realm(&*await!(model.root_realm.lock()), "echo");
            let actual_children = get_children(&*await!(echo_realm.lock()));
            assert!(actual_children.is_empty());
        }
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_instance_child_non_existent() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "root".to_string(),
            vec![ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));
        let offers = Arc::new(Mutex::new(HashMap::new()));
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        // bind to system
        {
            let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
                "system".to_string()
            ),])));
            let expected_res: Result<(), ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> = vec!["test:///system_resolved".to_string()];
            assert_eq!(*actual_uris, expected_uris);
        }
        // can't bind to logger: it does not exist
        {
            let moniker = AbsoluteMoniker::new(vec![
                ChildMoniker::new("system".to_string()),
                ChildMoniker::new("logger".to_string()),
            ]);
            let res = await!(model.bind_instance(moniker.clone()));
            let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(moniker));
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> = vec!["test:///system_resolved".to_string()];
            assert_eq!(*actual_uris, expected_uris);
        }
    }
    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_instance_eager_child() {
        // Create a hierarchy of children. The two components in the middle enable eager binding.
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "root".to_string(),
            vec![ChildInfo { name: "a".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        await!(children.lock()).insert(
            "a".to_string(),
            vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Eager }],
        );
        await!(children.lock()).insert(
            "b".to_string(),
            vec![ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Eager }],
        );
        await!(children.lock()).insert(
            "c".to_string(),
            vec![ChildInfo { name: "d".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));
        let offers = Arc::new(Mutex::new(HashMap::new()));
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });

        // Bind to the top component, and check that it and the eager components were started.
        {
            let res = await!(model
                .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("a".to_string()),])));
            let expected_res: Result<(), ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> = vec![
                "test:///a_resolved".to_string(),
                "test:///b_resolved".to_string(),
                "test:///c_resolved".to_string(),
            ];
            assert_eq!(*actual_uris, expected_uris);
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn bind_instance_recursive_child() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "root".to_string(),
            vec![ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        await!(children.lock()).insert(
            "system".to_string(),
            vec![
                ChildInfo { name: "logger".to_string(), startup: fsys::StartupMode::Lazy },
                ChildInfo { name: "netstack".to_string(), startup: fsys::StartupMode::Lazy },
            ],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));
        let offers = Arc::new(Mutex::new(HashMap::new()));
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        // bind to logger (before ever binding to system)
        {
            let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![
                ChildMoniker::new("system".to_string()),
                ChildMoniker::new("logger".to_string())
            ])));
            let expected_res: Result<(), ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> = vec!["test:///logger_resolved".to_string()];
            assert_eq!(*actual_uris, expected_uris);
        }
        // bind to netstack
        {
            let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![
                ChildMoniker::new("system".to_string()),
                ChildMoniker::new("netstack".to_string()),
            ])));
            let expected_res: Result<(), ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> = vec![
                "test:///logger_resolved".to_string(),
                "test:///netstack_resolved".to_string(),
            ];
            assert_eq!(*actual_uris, expected_uris);
        }
        // finally, bind to system
        {
            let res = await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
                "system".to_string()
            ),])));
            let expected_res: Result<(), ModelError> = Ok(());
            assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
            let actual_uris = await!(uris_run.lock());
            let expected_uris: Vec<String> = vec![
                "test:///logger_resolved".to_string(),
                "test:///netstack_resolved".to_string(),
                "test:///system_resolved".to_string(),
            ];
            assert_eq!(*actual_uris, expected_uris);
        }
        // validate children
        {
            let actual_children = get_children(&*await!(model.root_realm.lock()));
            let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
            expected_children.insert(ChildMoniker::new("system".to_string()));
            assert_eq!(actual_children, expected_children);
        }
        let system_realm = get_child_realm(&*await!(model.root_realm.lock()), "system");
        {
            let actual_children = get_children(&*await!(system_realm.lock()));
            let mut expected_children: HashSet<ChildMoniker> = HashSet::new();
            expected_children.insert(ChildMoniker::new("logger".to_string()));
            expected_children.insert(ChildMoniker::new("netstack".to_string()));
            assert_eq!(actual_children, expected_children);
        }
        {
            let logger_realm = get_child_realm(&*await!(system_realm.lock()), "logger");
            let actual_children = get_children(&*await!(logger_realm.lock()));
            assert!(actual_children.is_empty());
        }
        {
            let netstack_realm = get_child_realm(&*await!(system_realm.lock()), "netstack");
            let actual_children = get_children(&*await!(netstack_realm.lock()));
            assert!(actual_children.is_empty());
        }
    }

    // Installs a new directory at /hippo in our namespace. Does nothing if this directory already
    // exists.
    fn install_hippo_dir() {
        let (client_chan, server_chan) = zx::Channel::create().unwrap();

        let mut ns_ptr: *mut fdio::fdio_sys::fdio_ns_t = ptr::null_mut();
        let status = unsafe { fdio::fdio_sys::fdio_ns_get_installed(&mut ns_ptr) };
        if status != zx::sys::ZX_OK {
            panic!(
                "bad status returned for fdio_ns_get_installed: {}",
                zx::Status::from_raw(status)
            );
        }
        let cstr = CString::new("/hippo").unwrap();
        let status =
            unsafe { fdio::fdio_sys::fdio_ns_bind(ns_ptr, cstr.as_ptr(), client_chan.into_raw()) };
        if status != zx::sys::ZX_OK && status != zx::sys::ZX_ERR_ALREADY_EXISTS {
            panic!("bad status returned for fdio_ns_bind: {}", zx::Status::from_raw(status));
        }
        host_data_foo_hippo(ServerEnd::new(server_chan));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn check_namespace_from_using() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "root".to_string(),
            vec![ChildInfo { name: "system".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        // The system component will request namespaces of "/foo" to its "/bar" and
        // "/baz/bar", and it will also request the "/svc/fidl.examples.echo.Echo" service
        await!(uses.lock()).insert(
            "system".to_string(),
            Some(vec![
                fsys::UseDecl {
                    type_: Some(fsys::CapabilityType::Directory),
                    source_path: Some("/foo".to_string()),
                    target_path: Some("/bar".to_string()),
                },
                fsys::UseDecl {
                    type_: Some(fsys::CapabilityType::Directory),
                    source_path: Some("/foo".to_string()),
                    target_path: Some("/baz/bar".to_string()),
                },
                fsys::UseDecl {
                    type_: Some(fsys::CapabilityType::Service),
                    source_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
                    target_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
                },
            ]),
        );

        // The root component will offer the "/hippo" directory from its realm as "/foo" and the
        // "/svc/fidl.examples.echo.Echo" service from its realm
        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "root".to_string(),
            Some(vec![
                fsys::OfferDecl {
                    type_: Some(fsys::CapabilityType::Directory),
                    source_path: Some("/hippo".to_string()),
                    source: Some(fsys::RelativeId {
                        relation: Some(fsys::Relation::Realm),
                        child_name: None,
                    }),
                    targets: Some(vec![fsys::OfferTarget {
                        target_path: Some("/foo".to_string()),
                        child_name: Some("system".to_string()),
                    }]),
                },
                fsys::OfferDecl {
                    type_: Some(fsys::CapabilityType::Service),
                    source_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
                    source: Some(fsys::RelativeId {
                        relation: Some(fsys::Relation::Realm),
                        child_name: None,
                    }),
                    targets: Some(vec![fsys::OfferTarget {
                        target_path: Some("/svc/fidl.examples.echo.Echo".to_string()),
                        child_name: Some("system".to_string()),
                    }]),
                },
            ]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        // bind to root and system
        let _ = await!(model.bind_instance(AbsoluteMoniker::new(vec![])));
        let res = await!(model
            .bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("system".to_string()),])));
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(format!("{:?}", res), format!("{:?}", expected_res));
        // Verify system has the expected namespaces.
        let mut actual_namespaces = await!(namespaces.lock());
        assert!(actual_namespaces.contains_key("test:///system_resolved"));
        let ns = actual_namespaces.get_mut("test:///system_resolved").unwrap();
        // "/pkg" is missing because we don't supply package in MockResolver.
        assert_eq!(ns.paths, ["/bar", "/baz/bar", "/svc"]);
        assert_eq!(ns.directories.len(), 3);

        // Install /hippo in our namespace
        install_hippo_dir();

        // /bar and /baz/bar are both pointed to the root component's /foo. This is not backed by a
        // real running component in this case, so we can't use the directory as nothing will
        // respond. The /svc/fidl.examples.echo.Echo service is backed by a realm component in
        // componentmgr's namespace, so we can use it.
        while let Some(dir) = ns.directories.pop() {
            match ns.paths.pop().as_ref().map(|s| s.as_str()) {
                Some("/bar") | Some("/baz/bar") => {
                    let dir_proxy = dir.into_proxy().unwrap();
                    let path = PathBuf::from("data/foo/hippo");
                    let file_proxy =
                        io_util::open_file(&dir_proxy, &path).expect("failed to open file");
                    assert_eq!(
                        "hippo",
                        await!(io_util::read_file(&file_proxy)).expect("failed to read file")
                    );
                    return;
                }
                Some("/svc") => {
                    let dir_proxy = dir.into_proxy().unwrap();
                    let path = PathBuf::from("fidl.examples.echo.Echo");
                    let node_proxy = io_util::open_node(&dir_proxy, &path, MODE_TYPE_SERVICE)
                        .expect("failed to open echo service");
                    let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
                    let res = await!(echo_proxy.echo_string(Some("hippos")))
                        .expect("failed to use echo service");
                    assert_eq!(res, Some("hippos".to_string()));
                }
                Some(_) | None => panic!("unknown directory in namespace"),
            }
        }
    }
    fn get_children(realm: &Realm) -> HashSet<ChildMoniker> {
        realm.instance.child_realms.as_ref().unwrap().keys().map(|m| m.clone()).collect()
    }
    fn get_child_realm(realm: &Realm, child: &str) -> Arc<Mutex<Realm>> {
        realm.instance.child_realms.as_ref().unwrap()[&ChildMoniker::new(child.to_string())].clone()
    }

    fn host_svc_foo_hippo(server_end: ServerEnd<DirectoryMarker>) {
        let echo_service_fn = Box::new(move |server_end: ServerEnd<NodeMarker>| {
            fasync::spawn(
                async move {
                    let server_end: ServerEnd<EchoMarker> =
                        ServerEnd::new(server_end.into_channel());
                    let mut stream: EchoRequestStream = server_end.into_stream().unwrap();
                    while let Some(EchoRequest::EchoString { value, responder }) =
                        await!(stream.try_next()).unwrap()
                    {
                        responder.send(value.as_ref().map(|s| &**s)).unwrap();
                    }
                },
            );
        });
        fasync::spawn(
            async move {
                let mut pseudo_dir = pseudo_directory! {
                    "svc" => pseudo_directory! {
                        "foo" =>
                            directory_broker::DirectoryBroker::new_service_broker(echo_service_fn),
                    },
                };
                pseudo_dir
                    .open(
                        OPEN_RIGHT_READABLE,
                        MODE_TYPE_DIRECTORY,
                        &mut iter::empty(),
                        ServerEnd::new(server_end.into_channel()),
                    )
                    .expect("failed to open services dir");

                let _ = await!(pseudo_dir);
                panic!("the pseudo dir exited!");
            },
        );
    }

    async fn call_svc_hippo(ns: &mut fsys::ComponentNamespace) {
        while let Some(dir) = ns.directories.pop() {
            if let Some("/svc") = ns.paths.pop().as_ref().map(|s| s.as_str()) {
                let dir_proxy = dir.into_proxy().unwrap();
                let path = PathBuf::from("hippo");
                let node_proxy = io_util::open_node(&dir_proxy, &path, MODE_TYPE_SERVICE)
                    .expect("failed to open echo service");
                let echo_proxy = echo::EchoProxy::new(node_proxy.into_channel().unwrap());
                let res = await!(echo_proxy.echo_string(Some("hippos")))
                    .expect("failed to use echo service");
                assert_eq!(res, Some("hippos".to_string()));
                return;
            }
        }
        panic!("didn't find /svc");
    }

    // TODO: cover services and directories in the same tests below, as in
    // "use_service_and_directory_from_parent".
    ///   a
    ///    \
    ///     b
    ///
    /// a: offers service /svc/foo from self as /svc/bar
    /// b: uses service /svc/bar as /svc/hippo
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_service_from_parent() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/bar".to_string()),
                target_path: Some("/svc/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/svc/bar".to_string()),
                    child_name: Some("b".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
        assert!(await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        )
        .is_ok());

        // Host /svc/foo from a's outgoing directory
        host_svc_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///a_resolved").unwrap());

        // use /svc/baz from b's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(call_svc_hippo(namespaces.get_mut("test:///b_resolved").unwrap()));
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: offers service /svc/foo from self as /svc/bar
    /// b: offers service /svc/bar from realm as /svc/baz
    /// c: uses /svc/baz as /svc/hippo
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_service_from_grandparent() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        await!(children.lock()).insert(
            "b".to_string(),
            vec![ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "c".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/baz".to_string()),
                target_path: Some("/svc/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/svc/bar".to_string()),
                    child_name: Some("b".to_string()),
                }]),
            }]),
        );
        await!(offers.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/bar".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Realm),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/svc/baz".to_string()),
                    child_name: Some("c".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
        assert!(await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        )
        .is_ok());
        assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![
            ChildMoniker::new("b".to_string()),
            ChildMoniker::new("c".to_string()),
        ])))
        .is_ok());

        // Host /svc/foo from a's outgoing directory
        host_svc_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///a_resolved").unwrap());

        // use /svc/hippo from c's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(call_svc_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// b: exposes service /svc/foo from self as /svc/bar
    /// a: offers service /svc/bar from b as /svc/baz to c
    /// c: uses /svc/baz as /svc/hippo
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_service_from_sibling() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![
                ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
                ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
            ],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "c".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/baz".to_string()),
                target_path: Some("/svc/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/bar".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Child),
                    child_name: Some("b".to_string()),
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/svc/baz".to_string()),
                    child_name: Some("c".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        await!(exposes.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::ExposeDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                target_path: Some("/svc/bar".to_string()),
            }]),
        );
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        ).expect("failed to bind to b");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),]))
        ).expect("failed to bind to c");

        // Host /svc/foo from b's outgoing directory
        host_svc_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///b_resolved").unwrap());

        // use /svc/hippo from c's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(call_svc_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
    }

    ///     a
    ///    / \
    ///   b   c
    ///  /
    /// d
    ///
    /// d: exposes service /svc/foo from self as /svc/bar
    /// b: exposes service /svc/bar from d as /svc/baz
    /// a: offers service /svc/baz from b as /svc/foobar to c
    /// c: uses /svc/foobar as /svc/hippo
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_service_from_niece() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![
                ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
                ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
            ],
        );
        await!(children.lock()).insert(
            "b".to_string(),
            vec![ChildInfo { name: "d".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "c".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/foobar".to_string()),
                target_path: Some("/svc/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/baz".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Child),
                    child_name: Some("b".to_string()),
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/svc/foobar".to_string()),
                    child_name: Some("c".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        await!(exposes.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::ExposeDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/bar".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Child),
                    child_name: Some("d".to_string()),
                }),
                target_path: Some("/svc/baz".to_string()),
            }]),
        );
        await!(exposes.lock()).insert(
            "d".to_string(),
            Some(vec![fsys::ExposeDecl {
                type_: Some(fsys::CapabilityType::Service),
                source_path: Some("/svc/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                target_path: Some("/svc/bar".to_string()),
            }]),
        );
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        ).expect("failed to bind to b");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),]))
        ).expect("failed to bind to c");
        await!(model.bind_instance(AbsoluteMoniker::new(vec![
            ChildMoniker::new("b".to_string()),
            ChildMoniker::new("d".to_string()),
        ])))
        .expect("failed to bind to b/d");

        // Host /svc/foo from d's outgoing directory
        host_svc_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///d_resolved").unwrap());

        // use /svc/hippo from c's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(call_svc_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
    }

    fn host_data_foo_hippo(server_end: ServerEnd<DirectoryMarker>) {
        fasync::spawn(
            async move {
                let mut pseudo_dir = pseudo_directory! {
                    "data" => pseudo_directory! {
                        "foo" => pseudo_directory! {
                            "hippo" => read_only(|| Ok(b"hippo".to_vec())),
                        },
                    },
                };
                pseudo_dir
                    .open(
                        OPEN_RIGHT_READABLE,
                        MODE_TYPE_DIRECTORY,
                        &mut iter::empty(),
                        ServerEnd::new(server_end.into_channel()),
                    )
                    .expect("failed to open out dir");

                let _ = await!(pseudo_dir);

                panic!("the pseudo dir exited!");
            },
        );
    }

    async fn read_data_hippo_hippo(ns: &mut fsys::ComponentNamespace) {
        while let Some(dir) = ns.directories.pop() {
            if let Some("/data/hippo") = ns.paths.pop().as_ref().map(|s| s.as_str()) {
                let dir_proxy = dir.into_proxy().unwrap();
                let path = PathBuf::from("hippo");
                let file_proxy =
                    io_util::open_file(&dir_proxy, &path).expect("failed to open file");
                assert_eq!(
                    "hippo",
                    await!(io_util::read_file(&file_proxy)).expect("failed to read file")
                );
                return;
            }
        }
        panic!("didn't find /data/hippo");
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: offers directory /data/foo from self as /data/bar
    /// b: uses directory /data/bar as /data/hippo
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_directory_from_parent() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/bar".to_string()),
                target_path: Some("/data/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/data/bar".to_string()),
                    child_name: Some("b".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
        assert!(await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        )
        .is_ok());

        // Host /data/foo from a's outgoing directory
        host_data_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///a_resolved").unwrap());

        // use /data/hippo from b's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(read_data_hippo_hippo(namespaces.get_mut("test:///b_resolved").unwrap()));
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: offers directory /data/foo from self as /data/bar
    /// b: offers directory /data/bar from realm as /data/baz
    /// c: uses /svc/baz as /data/hippo
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_directory_from_grandparent() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        await!(children.lock()).insert(
            "b".to_string(),
            vec![ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "c".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/baz".to_string()),
                target_path: Some("/data/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/data/bar".to_string()),
                    child_name: Some("b".to_string()),
                }]),
            }]),
        );
        await!(offers.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/bar".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Realm),
                    child_name: None,
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/data/baz".to_string()),
                    child_name: Some("c".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).is_ok());
        assert!(await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        )
        .is_ok());
        assert!(await!(model.bind_instance(AbsoluteMoniker::new(vec![
            ChildMoniker::new("b".to_string()),
            ChildMoniker::new("c".to_string()),
        ])))
        .is_ok());

        // Host /data/foo from a's outgoing directory
        host_data_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///a_resolved").unwrap());

        // use /data/hippo from c's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(read_data_hippo_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// b: exposes directory /data/foo from self as /data/bar
    /// a: offers directory /data/bar from b as /data/baz to c
    /// c: uses /data/baz as /data/hippo
    #[fuchsia_async::run_singlethreaded(test)]
    async fn use_directory_from_sibling() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![
                ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
                ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
            ],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "c".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/baz".to_string()),
                target_path: Some("/data/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/bar".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Child),
                    child_name: Some("b".to_string()),
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/data/baz".to_string()),
                    child_name: Some("c".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        await!(exposes.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::ExposeDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                target_path: Some("/data/bar".to_string()),
            }]),
        );
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        ).expect("failed to bind to b");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),]))
        ).expect("failed to bind to c");

        // Host /data/foo from b's outgoing directory
        host_data_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///b_resolved").unwrap());

        // use /data/hippo from c's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(read_data_hippo_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
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
    async fn use_directory_from_niece() {
        let mut resolver = ResolverRegistry::new();
        let uris_run = Arc::new(Mutex::new(vec![]));
        let namespaces = Arc::new(Mutex::new(HashMap::new()));
        let outgoing_dirs = Arc::new(Mutex::new(HashMap::new()));
        let runner = MockRunner {
            uris_run: uris_run.clone(),
            namespaces: namespaces.clone(),
            outgoing_dirs: outgoing_dirs.clone(),
        };
        let children = Arc::new(Mutex::new(HashMap::new()));
        await!(children.lock()).insert(
            "a".to_string(),
            vec![
                ChildInfo { name: "b".to_string(), startup: fsys::StartupMode::Lazy },
                ChildInfo { name: "c".to_string(), startup: fsys::StartupMode::Lazy },
            ],
        );
        await!(children.lock()).insert(
            "b".to_string(),
            vec![ChildInfo { name: "d".to_string(), startup: fsys::StartupMode::Lazy }],
        );
        let uses = Arc::new(Mutex::new(HashMap::new()));

        await!(uses.lock()).insert(
            "c".to_string(),
            Some(vec![fsys::UseDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/foobar".to_string()),
                target_path: Some("/data/hippo".to_string()),
            }]),
        );

        let offers = Arc::new(Mutex::new(HashMap::new()));
        await!(offers.lock()).insert(
            "a".to_string(),
            Some(vec![fsys::OfferDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/baz".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Child),
                    child_name: Some("b".to_string()),
                }),
                targets: Some(vec![fsys::OfferTarget {
                    target_path: Some("/data/foobar".to_string()),
                    child_name: Some("c".to_string()),
                }]),
            }]),
        );
        let exposes = Arc::new(Mutex::new(HashMap::new()));
        await!(exposes.lock()).insert(
            "b".to_string(),
            Some(vec![fsys::ExposeDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/bar".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Child),
                    child_name: Some("d".to_string()),
                }),
                target_path: Some("/data/baz".to_string()),
            }]),
        );
        await!(exposes.lock()).insert(
            "d".to_string(),
            Some(vec![fsys::ExposeDecl {
                type_: Some(fsys::CapabilityType::Directory),
                source_path: Some("/data/foo".to_string()),
                source: Some(fsys::RelativeId {
                    relation: Some(fsys::Relation::Myself),
                    child_name: None,
                }),
                target_path: Some("/data/bar".to_string()),
            }]),
        );
        resolver.register(
            "test".to_string(),
            Box::new(MockResolver {
                children: children.clone(),
                uses: uses.clone(),
                offers: offers.clone(),
                exposes: exposes.clone(),
            }),
        );
        let model = Model::new(ModelParams {
            root_component_uri: "test:///a".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(runner),
        });
        //TODO: remove these binds once we have lazy binding.
        await!(model.bind_instance(AbsoluteMoniker::new(vec![]))).expect("failed to bind to root");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("b".to_string()),]))
        ).expect("failed to bind to b");
        await!(
            model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new("c".to_string()),]))
        ).expect("failed to bind to c");
        await!(model.bind_instance(AbsoluteMoniker::new(vec![
            ChildMoniker::new("b".to_string()),
            ChildMoniker::new("d".to_string()),
        ])))
        .expect("failed to bind to b/d");

        // Host /data/foo from d's outgoing directory
        host_data_foo_hippo(await!(outgoing_dirs.lock()).remove("test:///d_resolved").unwrap());

        // use /svc/hippo from c's incoming namespace
        let mut namespaces = await!(namespaces.lock());
        await!(read_data_hippo_hippo(namespaces.get_mut("test:///c_resolved").unwrap()));
    }
}
