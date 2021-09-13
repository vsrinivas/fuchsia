// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow,
    cm_rust::{FidlIntoNative, NativeIntoFidl},
    fidl::endpoints::{ProtocolMarker, RequestStream},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_data as fdata,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_realm_builder as frealmbuilder, fidl_fuchsia_sys2 as fsys,
    fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_syslog as syslog,
    futures::{future::BoxFuture, FutureExt, StreamExt, TryStreamExt},
    io_util,
    lazy_static::lazy_static,
    log::*,
    std::{
        collections::HashMap,
        convert::{TryFrom, TryInto},
        fmt::{self, Display},
        sync::Arc,
    },
    thiserror::{self, Error},
    url::Url,
};

mod resolver;
mod runner;

lazy_static! {
    pub static ref BINDER_PROTOCOL_CAPABILITY: frealmbuilder::Capability =
        frealmbuilder::Capability::Protocol(frealmbuilder::ProtocolCapability {
            name: Some(fcomponent::BinderMarker::DEBUG_NAME.to_owned()),
            ..frealmbuilder::ProtocolCapability::EMPTY
        });
}

#[fasync::run_singlethreaded()]
async fn main() {
    syslog::init_with_tags(&["fuchsia_component_test_framework_intermediary"])
        .expect("failed to init logging");
    info!("started");

    let mut fs = fserver::ServiceFs::new_local();
    let registry = resolver::Registry::new();
    let runner = runner::Runner::new();

    let registry_clone = registry.clone();
    fs.dir("svc").add_fidl_service(move |stream| registry_clone.run_resolver_service(stream));

    let runner_clone = runner.clone();
    fs.dir("svc").add_fidl_service(move |stream| runner_clone.run_runner_service(stream));

    fs.dir("svc").add_fidl_service(move |stream| {
        let registry = registry.clone();
        let runner = runner.clone();
        fasync::Task::local(async move {
            if let Err(e) = handle_framework_intermediary_stream(stream, registry, runner).await {
                error!("error encountered while running framework intermediary service: {:?}", e);
            }
        })
        .detach();
    });

    fs.take_and_serve_directory_handle().expect("did not receive directory handle");
    fs.collect::<()>().await;
}

async fn handle_framework_intermediary_stream(
    mut stream: frealmbuilder::FrameworkIntermediaryRequestStream,
    registry: Arc<resolver::Registry>,
    runner: Arc<runner::Runner>,
) -> Result<(), anyhow::Error> {
    let mut realm_tree = RealmNode::default();
    let mut test_pkg_dir = None;
    while let Some(req) = stream.try_next().await? {
        match req {
            frealmbuilder::FrameworkIntermediaryRequest::Init { pkg_dir_handle, responder } => {
                if test_pkg_dir.is_some() {
                    responder.send(&mut Err(Error::PkgDirAlreadySet.log_and_convert()))?;
                } else {
                    test_pkg_dir = Some(
                        pkg_dir_handle.into_proxy().expect("failed to convert ClientEnd to proxy"),
                    );
                    responder.send(&mut Ok(()))?;
                }
            }
            frealmbuilder::FrameworkIntermediaryRequest::SetComponent {
                moniker,
                component,
                responder,
            } => {
                match realm_tree
                    .set_component(moniker.clone().into(), component.clone(), &test_pkg_dir)
                    .await
                {
                    Ok(()) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        warn!(
                            "error occurred when setting component {:?} to {:?}",
                            moniker, component
                        );
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
            frealmbuilder::FrameworkIntermediaryRequest::SetMockComponent {
                moniker,
                responder,
            } => {
                let mock_id = runner.register_mock(stream.control_handle()).await;
                match realm_tree.set_mock_component(moniker.clone().into(), mock_id.clone()).await {
                    Ok(()) => responder.send(&mut Ok(mock_id.into()))?,
                    Err(e) => {
                        warn!("error occurred when setting mock component {:?}: {:?}", moniker, e);
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
            frealmbuilder::FrameworkIntermediaryRequest::GetComponentDecl {
                moniker,
                responder,
            } => match realm_tree.get_component_decl(moniker.clone().into()) {
                Ok(decl) => responder.send(&mut Ok(decl.native_into_fidl()))?,
                Err(e) => {
                    warn!("error occurred when getting decl for component {:?}", moniker);
                    responder.send(&mut Err(e.log_and_convert()))?;
                }
            },
            frealmbuilder::FrameworkIntermediaryRequest::RouteCapability { route, responder } => {
                match realm_tree.route_capability(route.clone()) {
                    Ok(()) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        warn!("error occurred when routing capability: {:?}", route);
                        responder.send(&mut Err(e.log_and_convert()))?
                    }
                }
            }
            frealmbuilder::FrameworkIntermediaryRequest::MarkAsEager { moniker, responder } => {
                match realm_tree.mark_as_eager(moniker.clone().into()) {
                    Ok(()) => responder.send(&mut Ok(()))?,
                    Err(e) => {
                        warn!("error occurred when marking {:?} as eager", moniker);
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
            frealmbuilder::FrameworkIntermediaryRequest::Contains { moniker, responder } => {
                responder.send(realm_tree.contains(moniker.clone().into()))?;
            }
            frealmbuilder::FrameworkIntermediaryRequest::Commit { responder } => {
                match realm_tree
                    .clone()
                    .commit(registry.clone(), vec![], test_pkg_dir.clone())
                    .await
                {
                    Ok(url) => responder.send(&mut Ok(url))?,
                    Err(e) => {
                        warn!("error occurred when committing");
                        responder.send(&mut Err(e.log_and_convert()))?;
                    }
                }
            }
        }
    }
    Ok(())
}

#[derive(Debug, Error)]
enum Error {
    #[error("unable to access components behind ChildDecls: {0}")]
    NodeBehindChildDecl(Moniker),

    #[error("component child doesn't exist: {0}")]
    NoSuchChild(String),

    #[error("unable to set the root component to a URL")]
    RootCannotBeSetToUrl,

    #[error("unable to set the root component as eager")]
    RootCannotBeEager,

    #[error("received malformed FIDL")]
    BadFidl,

    #[error("bad request: missing field {0}")]
    MissingField(&'static str),

    #[error("route targets cannot be empty")]
    RouteTargetsEmpty,

    #[error("the route source does not exist: {0}")]
    MissingRouteSource(Moniker),

    #[error("the route target does not exist: {0}")]
    MissingRouteTarget(Moniker),

    #[error("a route's target cannot be equal to its source: {0:?}")]
    RouteSourceAndTargetMatch(frealmbuilder::RouteEndpoint),

    #[error("can only use protocols from debug: {0:?}")]
    InvalidCapabilityFromDebug(Moniker),

    #[error("the component decl for {0} failed validation: {1:?}")]
    ValidationError(Moniker, cm_fidl_validator::ErrorList),

    #[error("{0} capabilities cannot be exposed")]
    UnableToExpose(&'static str),

    #[error("storage capabilities must come from above root")]
    StorageSourceInvalid,

    #[error("component with moniker {0} does not exist")]
    MonikerNotFound(Moniker),

    #[error("the package directory has already been set for this connection")]
    PkgDirAlreadySet,

    #[error("unable to load component from package, the package dir is not set")]
    PkgDirNotSet,

    #[error("failed to load component from package due to IO error")]
    PkgDirIoError(io_util::node::OpenError),

    #[error("failed to load component decl")]
    FailedToLoadComponentDecl(anyhow::Error),
}

impl Error {
    fn log_and_convert(self) -> frealmbuilder::RealmBuilderError {
        warn!("sending error to client: {:?}", self);
        match self {
            Error::NodeBehindChildDecl(_) => frealmbuilder::RealmBuilderError::NodeBehindChildDecl,
            Error::NoSuchChild(_) => frealmbuilder::RealmBuilderError::NoSuchChild,
            Error::RootCannotBeSetToUrl => frealmbuilder::RealmBuilderError::RootCannotBeSetToUrl,
            Error::RootCannotBeEager => frealmbuilder::RealmBuilderError::RootCannotBeEager,
            Error::BadFidl => frealmbuilder::RealmBuilderError::BadFidl,
            Error::MissingField(_) => frealmbuilder::RealmBuilderError::MissingField,
            Error::RouteTargetsEmpty => frealmbuilder::RealmBuilderError::RouteTargetsEmpty,
            Error::MissingRouteSource(_) => frealmbuilder::RealmBuilderError::MissingRouteSource,
            Error::MissingRouteTarget(_) => frealmbuilder::RealmBuilderError::MissingRouteTarget,
            Error::RouteSourceAndTargetMatch(_) => {
                frealmbuilder::RealmBuilderError::RouteSourceAndTargetMatch
            }
            Error::ValidationError(_, _) => frealmbuilder::RealmBuilderError::ValidationError,
            Error::UnableToExpose(_) => frealmbuilder::RealmBuilderError::UnableToExpose,
            Error::StorageSourceInvalid => frealmbuilder::RealmBuilderError::StorageSourceInvalid,
            Error::MonikerNotFound(_) => frealmbuilder::RealmBuilderError::MonikerNotFound,
            Error::PkgDirAlreadySet => frealmbuilder::RealmBuilderError::PkgDirAlreadySet,
            Error::PkgDirNotSet => frealmbuilder::RealmBuilderError::PkgDirNotSet,
            Error::PkgDirIoError(_) => frealmbuilder::RealmBuilderError::PkgDirIoError,
            Error::FailedToLoadComponentDecl(_) => {
                frealmbuilder::RealmBuilderError::FailedToLoadComponentDecl
            }
            Error::InvalidCapabilityFromDebug(_) => {
                frealmbuilder::RealmBuilderError::InvalidCapabilityFromDebug
            }
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq)]
struct RealmNode {
    decl: cm_rust::ComponentDecl,
    eager: bool,
    environment: Option<String>,

    /// When a component decl comes directly from the test package directory, we should check the
    /// component's manifest during route generation to see if it matches our expectations, instead
    /// of blindly pushing things into it. This way we can detect common issues like "the source
    /// component doesn't declare that capability".
    component_loaded_from_pkg: bool,

    /// Children stored in this HashMap can be mutated. Children stored in `decl.children` can not.
    /// Any children stored in `mutable_children` do NOT have a corresponding `ChildDecl` stored in
    /// `decl.children`, the two should be fully mutually exclusive.
    ///
    /// Suitable `ChildDecl`s for the contents of `mutable_children` are generated and added to
    /// `decl.children` when `commit()` is called.
    mutable_children: HashMap<String, RealmNode>,
}

#[derive(PartialEq)]
enum GetBehavior {
    CreateIfMissing,
    ErrorIfMissing,
}

impl RealmNode {
    fn child<'a>(&'a mut self, child_name: &String) -> Result<&'a mut Self, Error> {
        self.mutable_children.get_mut(child_name).ok_or(Error::NoSuchChild(child_name.clone()))
    }

    fn child_create_if_missing<'a>(&'a mut self, child_name: &String) -> &'a mut Self {
        if !self.mutable_children.contains_key(child_name) {
            self.mutable_children.insert(child_name.clone(), RealmNode::default());
        }
        self.child(child_name).unwrap()
    }

    /// Calls `cm_fidl_validator` on this node's decl, filtering out any errors caused by
    /// missing ChildDecls, as these children may be added to the mutable_children list at a later
    /// point. These decls are re-validated (without filtering out errors) during `commit()`.
    /// `moniker` is used for error reporting.
    fn validate(&self, moniker: &Moniker) -> Result<(), Error> {
        if let Err(mut e) = cm_fidl_validator::validate(&self.decl.clone().native_into_fidl()) {
            e.errs = e
                .errs
                .into_iter()
                .filter(|e| match e {
                    cm_fidl_validator::Error::InvalidChild(_, _) => false,
                    _ => true,
                })
                .collect();
            if !e.errs.is_empty() {
                return Err(Error::ValidationError(moniker.clone(), e));
            }
        }
        Ok(())
    }

    fn get_node_mut<'a>(
        &'a mut self,
        moniker: &Moniker,
        behavior: GetBehavior,
    ) -> Result<&'a mut RealmNode, Error> {
        let mut current_node = self;
        for part in moniker.path() {
            if current_node.decl.children.iter().any(|c| c.name == part.to_string()) {
                return Err(Error::NodeBehindChildDecl(moniker.clone()));
            }
            current_node = match behavior {
                GetBehavior::CreateIfMissing => current_node.child_create_if_missing(part),
                GetBehavior::ErrorIfMissing => current_node.child(part)?,
            }
        }
        Ok(current_node)
    }

    /// Returns true if the component exists in this realm.
    fn contains(&mut self, moniker: Moniker) -> bool {
        // The root node is an edge case. If the client hasn't set or modified the root
        // component in any way it should expect the intermediary to state that the root
        // component doesn't exist yet, but in this implementation the root node _always_
        // exists. If we're checking for the root component and we're equal to the default
        // RealmNode (aka there are no children and our decl is empty), then we return false.
        if moniker.is_root() && self == &mut RealmNode::default() {
            return false;
        }
        if let Ok(_) = self.get_node_mut(&moniker, GetBehavior::ErrorIfMissing) {
            return true;
        }
        // `get_node_mut` only returns `Ok` for mutable nodes. This node could still be in our
        // realm but be immutable, so let's check for that.
        if let Some(parent_moniker) = moniker.parent() {
            if let Ok(parent_node) = self.get_node_mut(&parent_moniker, GetBehavior::ErrorIfMissing)
            {
                let child_name = moniker.child_name().unwrap().to_string();
                let res = parent_node.decl.children.iter().any(|c| c.name == child_name);
                return res;
            }
            // If the parent node doesn't exist, then the component itself obviously does not
            // either.
            return false;
        } else {
            // The root component always exists
            return true;
        }
    }

    /// Sets the component to the provided component source. If the source is
    /// a `Component::decl` then a new node is added to the internal tree
    /// structure maintained for this connection. If the source is a
    /// `Component::url` then a new ChildDecl is added to the parent of the
    /// moniker. If any parents for the component do not exist then they are
    /// added. If a different component already exists under this moniker,
    /// then it is replaced.
    async fn set_component(
        &mut self,
        moniker: Moniker,
        component: frealmbuilder::Component,
        test_pkg_dir: &Option<DirectoryProxy>,
    ) -> Result<(), Error> {
        match component {
            frealmbuilder::Component::Decl(decl) => {
                if let Some(parent_moniker) = moniker.parent() {
                    let parent_node =
                        self.get_node_mut(&parent_moniker, GetBehavior::CreateIfMissing)?;
                    let child_name = moniker.child_name().unwrap().to_string();
                    parent_node.decl.children = parent_node
                        .decl
                        .children
                        .iter()
                        .filter(|c| c.name != child_name)
                        .cloned()
                        .collect();
                }
                let node = self.get_node_mut(&moniker, GetBehavior::CreateIfMissing)?;
                node.decl = decl.fidl_into_native();
                node.validate(&moniker)?;
            }
            frealmbuilder::Component::Url(url) => {
                if is_relative_url(&url) {
                    return self
                        .load_decl_from_pkg(
                            moniker,
                            url,
                            test_pkg_dir.as_ref().cloned().ok_or(Error::PkgDirNotSet)?,
                        )
                        .await;
                }
                if moniker.is_root() {
                    return Err(Error::RootCannotBeSetToUrl);
                }
                let parent_node =
                    self.get_node_mut(&moniker.parent().unwrap(), GetBehavior::CreateIfMissing)?;
                let child_name = moniker.child_name().unwrap().to_string();
                parent_node.mutable_children.remove(&child_name);
                parent_node.decl.children = parent_node
                    .decl
                    .children
                    .iter()
                    .filter(|c| c.name != child_name)
                    .cloned()
                    .collect();
                parent_node.decl.children.push(cm_rust::ChildDecl {
                    name: child_name,
                    url,
                    startup: fsys::StartupMode::Lazy,
                    environment: None,
                    on_terminate: None,
                });
            }
            frealmbuilder::Component::LegacyUrl(url) => {
                if let Some(parent_moniker) = moniker.parent() {
                    let parent_node =
                        self.get_node_mut(&parent_moniker, GetBehavior::CreateIfMissing)?;
                    let child_name = moniker.child_name().unwrap().to_string();
                    parent_node.decl.children = parent_node
                        .decl
                        .children
                        .iter()
                        .filter(|c| c.name != child_name)
                        .cloned()
                        .collect();
                }
                let node = self.get_node_mut(&moniker, GetBehavior::CreateIfMissing)?;
                node.decl = cm_rust::ComponentDecl {
                    program: Some(cm_rust::ProgramDecl {
                        runner: Some(crate::runner::RUNNER_NAME.try_into().unwrap()),
                        info: fdata::Dictionary {
                            entries: Some(vec![fdata::DictionaryEntry {
                                key: runner::LEGACY_URL_KEY.to_string(),
                                value: Some(Box::new(fdata::DictionaryValue::Str(url))),
                            }]),
                            ..fdata::Dictionary::EMPTY
                        },
                    }),
                    ..cm_rust::ComponentDecl::default()
                };
                node.validate(&moniker)?;
            }
            _ => return Err(Error::BadFidl),
        }
        Ok(())
    }

    /// Sets the component to be a mock component. A new ComponentDecl is generated for the
    /// component, and assigned a new mock id.
    async fn set_mock_component(
        &mut self,
        moniker: Moniker,
        mock_id: runner::MockId,
    ) -> Result<(), Error> {
        if let Some(parent_moniker) = moniker.parent() {
            let parent_node = self.get_node_mut(&parent_moniker, GetBehavior::CreateIfMissing)?;
            let child_name = moniker.child_name().unwrap().to_string();
            parent_node.decl.children = parent_node
                .decl
                .children
                .iter()
                .filter(|c| c.name != child_name)
                .cloned()
                .collect();
        }
        let decl = cm_rust::ComponentDecl {
            program: Some(cm_rust::ProgramDecl {
                runner: Some(runner::RUNNER_NAME.try_into().unwrap()),
                info: fdata::Dictionary {
                    entries: Some(vec![fdata::DictionaryEntry {
                        key: runner::MOCK_ID_KEY.to_string(),
                        value: Some(Box::new(fdata::DictionaryValue::Str(mock_id.into()))),
                    }]),
                    ..fdata::Dictionary::EMPTY
                },
            }),
            ..cm_rust::ComponentDecl::default()
        };
        let node = self.get_node_mut(&moniker, GetBehavior::CreateIfMissing)?;
        node.decl = decl;
        node.validate(&moniker)?;
        Ok(())
    }

    /// Loads the file referenced by the relative url `url` from `test_pkg_dir`, and sets it as the
    /// decl for the component referred to by `moniker`. Also loads in the declarations for any
    /// additional relative URLs in the new decl in the same manner, and so forth until all
    /// relative URLs have been processed.
    async fn load_decl_from_pkg(
        &mut self,
        moniker: Moniker,
        url: String,
        test_pkg_dir: DirectoryProxy,
    ) -> Result<(), Error> {
        // This can't be written recursively, because we need async here and the resulting
        // BoxFuture would have to hold on to `&mut self`, which isn't possible because the
        // reference is not `'static`.
        //
        // This is also written somewhat inefficiently, because holding a reference to the current
        // working node in the stack would result to multiple mutable references from `&mut self`
        // being held at the same time, which is disallowed. As a result, this re-fetches the
        // current working node from the root of the tree on each iteration.
        let mut relative_urls_to_process = vec![(moniker, url)];
        while let Some((current_moniker, relative_url)) = relative_urls_to_process.pop() {
            let current_node = self.get_node_mut(&current_moniker, GetBehavior::CreateIfMissing)?;

            // Load the decl and validate it
            let path = relative_url.trim_start_matches('#');
            let file_proxy =
                io_util::directory::open_file(&test_pkg_dir, &path, io_util::OPEN_RIGHT_READABLE)
                    .await
                    .map_err(Error::PkgDirIoError)?;
            let fidl_decl = io_util::read_file_fidl::<fsys::ComponentDecl>(&file_proxy)
                .await
                .map_err(Error::FailedToLoadComponentDecl)?;
            current_node.decl = fidl_decl.fidl_into_native();
            current_node.component_loaded_from_pkg = true;
            current_node.validate(&current_moniker)?;

            // Look through the new decl's children. If there are any relative URLs, we need to
            // handle those too.
            let mut child_decls_to_keep = vec![];
            let mut child_decls_to_load = vec![];
            for child in current_node.decl.children.drain(..) {
                if is_relative_url(&child.url) {
                    child_decls_to_load.push(child);
                } else {
                    child_decls_to_keep.push(child);
                }
            }
            current_node.decl.children = child_decls_to_keep;

            for child in child_decls_to_load {
                let child_node = current_node.child_create_if_missing(&child.name);
                let child_moniker = current_moniker.child(child.name.clone());
                if child.startup == fsys::StartupMode::Eager {
                    child_node.eager = true;
                }
                child_node.environment = child.environment;
                relative_urls_to_process.push((child_moniker, child.url));
            }
        }
        Ok(())
    }

    /// Returns the current value of a component decl in the realm being
    /// constructed. Note that this cannot retrieve decls through external
    /// URLs, so for example if `SetComponent` is called with `Component::url`
    /// and then `GetComponentDecl` is called with the same moniker, an error
    /// will be returned.
    fn get_component_decl(&mut self, moniker: Moniker) -> Result<cm_rust::ComponentDecl, Error> {
        Ok(self.get_node_mut(&moniker, GetBehavior::ErrorIfMissing)?.decl.clone())
    }

    /// Marks the component and any ancestors of it as eager, ensuring that the
    /// component is started immediately once the realm is bound to.
    fn mark_as_eager(&mut self, moniker: Moniker) -> Result<(), Error> {
        if moniker.is_root() {
            return Err(Error::RootCannotBeEager);
        }
        if !self.contains(moniker.clone()) {
            return Err(Error::MonikerNotFound(moniker.clone()));
        }
        // The component we want to mark as eager could be either mutable or immutable. Mutable
        // components are retrievable with `self.get_node_mut`, whereas immutable components are
        // found in a ChildDecl in the decl of the node's parent.
        if let Ok(node) = self.get_node_mut(&moniker, GetBehavior::ErrorIfMissing) {
            node.eager = true;
        }
        let parent_node =
            self.get_node_mut(&moniker.parent().unwrap(), GetBehavior::ErrorIfMissing)?;
        if let Some(child_decl) =
            parent_node.decl.children.iter_mut().find(|c| &c.name == moniker.child_name().unwrap())
        {
            child_decl.startup = fsys::StartupMode::Eager;
        }
        for ancestor in moniker.ancestry() {
            let ancestor_node = self.get_node_mut(&ancestor, GetBehavior::ErrorIfMissing)?;
            ancestor_node.eager = true;
        }
        Ok(())
    }

    /// Adds a capability route to the realm being constructed, adding any
    /// necessary offers, exposes, uses, and capability declarations to any
    /// component involved in the route. Note that components added with
    /// `Component::url` can not be modified, and they are presumed to already
    /// have the declarations needed for the route to be valid. If an error is
    /// returned some of the components in the route may have been updated while
    /// others were not.
    fn route_capability(&mut self, route: frealmbuilder::CapabilityRoute) -> Result<(), Error> {
        let capability = route.capability.ok_or(Error::MissingField("capability"))?;
        let source = route.source.ok_or(Error::MissingField("source"))?;
        let targets = route.targets.ok_or(Error::MissingField("targets"))?;
        if targets.is_empty() {
            return Err(Error::RouteTargetsEmpty);
        }
        if let frealmbuilder::RouteEndpoint::Component(moniker) = &source {
            let moniker: Moniker = moniker.clone().into();
            if !self.contains(moniker.clone()) {
                return Err(Error::MissingRouteSource(moniker.clone()));
            }
        }
        for target in &targets {
            if &source == target {
                return Err(Error::RouteSourceAndTargetMatch(source));
            }
            if let frealmbuilder::RouteEndpoint::Component(target_moniker) = target {
                let target_moniker: Moniker = target_moniker.clone().into();
                if !self.contains(target_moniker.clone()) {
                    return Err(Error::MissingRouteTarget(target_moniker));
                }
            }
        }
        let force_route = route.force_route.unwrap_or(false);
        for target in targets {
            if let frealmbuilder::RouteEndpoint::AboveRoot(_) = target {
                // We're routing a capability from component within our constructed realm to
                // somewhere above it
                self.route_capability_to_above_root(
                    &capability,
                    source.clone().try_into()?,
                    force_route,
                    cm_rust::ExposeSource::Self_,
                )?;
            } else if let frealmbuilder::RouteEndpoint::AboveRoot(_) = &source {
                // We're routing a capability from above our constructed realm to a component
                // within it
                self.route_capability_from_above_root(
                    &capability,
                    target.try_into()?,
                    force_route,
                )?;
            } else if let frealmbuilder::RouteEndpoint::Debug(_) = &source {
                // We're routing a capability from the debug section of the component's environment.
                self.route_capability_from_debug(&capability, target.try_into()?, force_route)?;
            } else {
                // We're routing a capability from one component within our constructed realm to
                // another
                let source_moniker = source.clone().try_into()?;
                let target_moniker: Moniker = target.try_into()?;
                if target_moniker.is_ancestor_of(&source_moniker) {
                    // The target is an ancestor of the source, so this is a "use from child"
                    // scenario
                    self.route_capability_use_from_child(
                        &capability,
                        source_moniker,
                        target_moniker,
                        force_route,
                    )?;
                } else {
                    // The target is _not_ an ancestor of the source, so this is a classic "routing
                    // between two components" scenario, where the target uses the capability from
                    // its parent.
                    self.route_capability_between_components(
                        &capability,
                        source_moniker,
                        target_moniker,
                        force_route,
                    )?;
                }
            }
        }
        Ok(())
    }

    fn route_capability_to_above_root(
        &mut self,
        capability: &frealmbuilder::Capability,
        source_moniker: Moniker,
        force_route: bool,
        from: cm_rust::ExposeSource,
    ) -> Result<(), Error> {
        let mut current_ancestor = self.get_node_mut(&Moniker::root(), GetBehavior::ErrorIfMissing);
        let mut current_moniker = Moniker::root();
        for child_name in source_moniker.path() {
            let current = current_ancestor?;
            current.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Child(child_name.to_string()),
                force_route,
            )?;

            current_ancestor = current.child(&child_name);
            current_moniker = current_moniker.child(child_name.clone());
        }

        if let Ok(source_node) = self.get_node_mut(&source_moniker, GetBehavior::ErrorIfMissing) {
            source_node.add_expose_for_capability(&capability, from, force_route)?;
            source_node.add_capability_decl(&capability, force_route)?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //source_node.validate(&source_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already declares
            // and exposes thecapability
        }
        Ok(())
    }

    fn route_capability_from_above_root(
        &mut self,
        capability: &frealmbuilder::Capability,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        let mut current_ancestor = self.get_node_mut(&Moniker::root(), GetBehavior::ErrorIfMissing);
        let mut current_moniker = Moniker::root();
        for child_name in target_moniker.path() {
            let current = current_ancestor?;
            current.add_offer_for_capability(
                &capability,
                cm_rust::OfferSource::Parent,
                &child_name,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_moniker)?;

            current_ancestor = current.child(&child_name);
            current_moniker = current_moniker.child(child_name.clone());
        }

        if let Ok(target_node) = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing) {
            target_node.add_use_for_capability(
                &capability,
                cm_rust::UseSource::Parent,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }
        Ok(())
    }

    fn route_capability_from_debug(
        &mut self,
        capability: &frealmbuilder::Capability,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        match &capability {
            frealmbuilder::Capability::Protocol(_) => { /*only this is supported */ }
            _ => return Err(Error::InvalidCapabilityFromDebug(target_moniker)),
        }
        if let Ok(target_node) = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing) {
            target_node.add_use_for_capability(
                &capability,
                cm_rust::UseSource::Debug,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }
        Ok(())
    }

    // This will panic if `target_moniker.is_ancestor_of(source_moniker)` returns false
    fn route_capability_use_from_child(
        &mut self,
        capability: &frealmbuilder::Capability,
        source_moniker: Moniker,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        let target_node = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing)?;
        let child_source = target_moniker.downward_path_to(&source_moniker).get(0).unwrap().clone();
        target_node.add_use_for_capability(
            &capability,
            cm_rust::UseSource::Child(child_source),
            force_route,
        )?;
        // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
        //target_node.validate(&target_moniker)?;

        let mut path_to_source = target_moniker.downward_path_to(&source_moniker);
        let first_expose_name = path_to_source.remove(0);
        let mut current_moniker = target_moniker.child(first_expose_name.clone());
        let mut current_node = target_node.child(&first_expose_name);
        for child_name in path_to_source {
            let current = current_node?;
            current.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Child(child_name.to_string()),
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_moniker)?;
            current_node = current.child(&child_name);
            current_moniker = current_moniker.child(child_name);
        }
        if let Ok(source_node) = current_node {
            source_node.add_capability_decl(&capability, force_route)?;
            source_node.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Self_,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //source_node.validate(&current_moniker)?;
        } else {
            // `RealmNode::child` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already declares
            // the capability.
        }
        Ok(())
    }

    fn route_capability_between_components(
        &mut self,
        capability: &frealmbuilder::Capability,
        source_moniker: Moniker,
        target_moniker: Moniker,
        force_route: bool,
    ) -> Result<(), Error> {
        if let Ok(target_node) = self.get_node_mut(&target_moniker, GetBehavior::ErrorIfMissing) {
            target_node.add_use_for_capability(
                &capability,
                cm_rust::UseSource::Parent,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }
        if let Ok(source_node) = self.get_node_mut(&source_moniker, GetBehavior::ErrorIfMissing) {
            source_node.add_capability_decl(&capability, force_route)?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //target_node.validate(&target_moniker)?;
        } else {
            // `get_node_mut` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already uses
            // the capability.
        }

        let mut common_ancestor_moniker = target_moniker.parent().unwrap();
        while common_ancestor_moniker != source_moniker
            && !common_ancestor_moniker.is_ancestor_of(&source_moniker)
        {
            common_ancestor_moniker = common_ancestor_moniker.parent().unwrap();
        }
        let common_ancestor =
            self.get_node_mut(&common_ancestor_moniker, GetBehavior::ErrorIfMissing)?;

        let mut path_to_target = common_ancestor_moniker.downward_path_to(&target_moniker);
        let first_offer_name = path_to_target.remove(0);
        let mut current_ancestor_moniker = common_ancestor_moniker.child(first_offer_name.clone());

        let mut current_node = common_ancestor.child(&first_offer_name);

        for child_name in path_to_target {
            let current = current_node?;
            current.add_offer_for_capability(
                &capability,
                cm_rust::OfferSource::Parent,
                &child_name,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_ancestor_moniker)?;
            current_node = current.child(&child_name);
            current_ancestor_moniker = current_ancestor_moniker.child(child_name.clone());
        }

        if common_ancestor_moniker == source_moniker {
            // We don't need to add an expose chain, we reached the source moniker solely
            // by walking up the tree
            let common_ancestor =
                self.get_node_mut(&common_ancestor_moniker, GetBehavior::ErrorIfMissing)?;
            common_ancestor.add_offer_for_capability(
                &capability,
                cm_rust::OfferSource::Self_,
                &first_offer_name,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //common_ancestor.validate(&common_ancestor_moniker)?;
            return Ok(());
        }

        // We need an expose chain to descend down the tree to our source.

        let mut path_to_target = common_ancestor_moniker.downward_path_to(&source_moniker);
        let first_expose_name = path_to_target.remove(0);
        let mut current_ancestor_moniker = common_ancestor_moniker.child(first_expose_name.clone());
        let mut current_node = common_ancestor.child(&first_expose_name);

        for child_name in path_to_target {
            let current = current_node?;
            current.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Child(child_name.to_string()),
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //current.validate(&current_ancestor_moniker)?;
            current_node = current.child(&child_name);
            current_ancestor_moniker = current_ancestor_moniker.child(child_name.clone());
        }

        if let Ok(source_node) = current_node {
            source_node.add_expose_for_capability(
                &capability,
                cm_rust::ExposeSource::Self_,
                force_route,
            )?;
            // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
            //source_node.validate(&current_ancestor_moniker)?;
        } else {
            // `RealmNode::child` only returns `Ok` for mutable nodes. If this node is immutable
            // (located behind a ChildDecl) we have to presume that the component already exposes
            // the capability.
        }

        common_ancestor.add_offer_for_capability(
            &capability,
            cm_rust::OfferSource::static_child(first_expose_name.clone()),
            &first_offer_name,
            force_route,
        )?;
        // TODO(fxbug.dev/74977): eagerly validate decls once weak routes are supported
        //common_ancestor.validate(&common_ancestor_moniker)?;
        Ok(())
    }

    /// Assembles the realm being constructed and returns the URL for the root
    /// component in the realm, which may then be used to create a new component
    /// in any collection where fuchsia-test-component is properly set up.
    fn commit(
        mut self,
        registry: Arc<resolver::Registry>,
        walked_path: Vec<String>,
        package_dir: Option<DirectoryProxy>,
    ) -> BoxFuture<'static, Result<String, Error>> {
        // This function is much cleaner written recursively, but we can't construct recursive
        // futures as the size isn't knowable to rustc at compile time. Put the recursive call
        // into a boxed future, as the redirection makes this possible
        async move {
            // Expose the fuchsia.component.Binder protocol from root in order to give users the ability to manually
            // start the realm.
            if walked_path.is_empty() {
                let () = self.route_capability_to_above_root(
                    &*BINDER_PROTOCOL_CAPABILITY,
                    Moniker::root(),
                    true,
                    cm_rust::ExposeSource::Framework,
                )?;
            }

            let mut mutable_children = self.mutable_children.into_iter().collect::<Vec<_>>();
            mutable_children.sort_unstable_by_key(|t| t.0.clone());
            for (name, node) in mutable_children {
                let mut new_path = walked_path.clone();
                new_path.push(name.clone());

                let startup =
                    if node.eager { fsys::StartupMode::Eager } else { fsys::StartupMode::Lazy };
                let environment = node.environment.clone();
                let url = node.commit(registry.clone(), new_path, package_dir.clone()).await?;
                self.decl.children.push(cm_rust::ChildDecl {
                    name,
                    url,
                    startup,
                    environment,
                    on_terminate: None,
                });
            }

            let decl = self.decl.native_into_fidl();
            registry
                .validate_and_register(decl, package_dir.clone())
                .await
                .map_err(|e| Error::ValidationError(walked_path.into(), e))
        }
        .boxed()
    }

    /// This call ensures that an expose for the given capability exists in this component's decl.
    /// If `self.component_loaded_from_pkg && !force_route` is true, we don't do anything.
    fn add_expose_for_capability(
        &mut self,
        capability: &frealmbuilder::Capability,
        source: cm_rust::ExposeSource,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        let capability_name = get_capability_name(&capability)?;
        let new_decl = {
            match &capability {
                frealmbuilder::Capability::Protocol(_) => {
                    cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                        source,
                        source_name: capability_name.clone().into(),
                        target: cm_rust::ExposeTarget::Parent,
                        target_name: capability_name.into(),
                    })
                }
                frealmbuilder::Capability::Directory(_) => {
                    cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                        source,
                        source_name: capability_name.clone().into(),
                        target: cm_rust::ExposeTarget::Parent,
                        target_name: capability_name.into(),
                        rights: None,
                        subdir: None,
                    })
                }
                frealmbuilder::Capability::Storage(frealmbuilder::StorageCapability { .. }) => {
                    return Err(Error::UnableToExpose("storage"));
                }
                _ => return Err(Error::BadFidl),
            }
        };
        // A decl with the same source and name but different options will be caught during decl
        // validation later
        if !self.decl.exposes.contains(&new_decl) {
            self.decl.exposes.push(new_decl);
        }

        Ok(())
    }

    /// This call ensures that a declaration for the given capability and source exists in this
    /// component's decl. If `self.component_loaded_from_pkg && !force_route` is true, we don't do
    /// anything.
    fn add_capability_decl(
        &mut self,
        capability: &frealmbuilder::Capability,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        let capability_name = get_capability_name(&capability)?;
        let capability_decl = match capability {
            frealmbuilder::Capability::Protocol(_) => {
                Some(cm_rust::CapabilityDecl::Protocol(cm_rust::ProtocolDecl {
                    name: capability_name.as_str().try_into().unwrap(),
                    source_path: Some(
                        format!("/svc/{}", capability_name).as_str().try_into().unwrap(),
                    ),
                }))
            }
            frealmbuilder::Capability::Directory(frealmbuilder::DirectoryCapability {
                path,
                rights,
                ..
            }) => Some(cm_rust::CapabilityDecl::Directory(cm_rust::DirectoryDecl {
                name: capability_name.as_str().try_into().unwrap(),
                source_path: Some(path.as_ref().unwrap().as_str().try_into().unwrap()),
                rights: rights.as_ref().unwrap().clone(),
            })),
            frealmbuilder::Capability::Storage(_) => {
                return Err(Error::StorageSourceInvalid);
            }
            _ => return Err(Error::BadFidl),
        };
        if let Some(decl) = capability_decl {
            // A decl with the same source and name but different options will be caught during
            // decl validation later
            if !self.decl.capabilities.contains(&decl) {
                self.decl.capabilities.push(decl);
            }
        }
        Ok(())
    }

    /// This call ensures that a use for the given capability exists in this component's decl. If
    /// `self.component_loaded_from_pkg && !force_route` is true, we don't do anything.
    fn add_use_for_capability(
        &mut self,
        capability: &frealmbuilder::Capability,
        use_source: cm_rust::UseSource,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        let capability_name = get_capability_name(&capability)?;
        let use_decl = match capability {
            frealmbuilder::Capability::Protocol(_) => {
                cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                    source: use_source,
                    source_name: capability_name.as_str().try_into().unwrap(),
                    target_path: format!("/svc/{}", capability_name).as_str().try_into().unwrap(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })
            }
            frealmbuilder::Capability::Directory(frealmbuilder::DirectoryCapability {
                path,
                rights,
                ..
            }) => cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                source: use_source,
                source_name: capability_name.as_str().try_into().unwrap(),
                target_path: path.as_ref().unwrap().as_str().try_into().unwrap(),
                rights: rights.as_ref().unwrap().clone(),
                subdir: None,
                dependency_type: cm_rust::DependencyType::Strong,
            }),
            frealmbuilder::Capability::Storage(frealmbuilder::StorageCapability {
                path, ..
            }) => {
                if use_source != cm_rust::UseSource::Parent {
                    return Err(Error::UnableToExpose("storage"));
                }
                cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl {
                    source_name: capability_name.as_str().try_into().unwrap(),
                    target_path: path.as_ref().unwrap().as_str().try_into().unwrap(),
                })
            }
            _ => return Err(Error::BadFidl),
        };
        if !self.decl.uses.contains(&use_decl) {
            self.decl.uses.push(use_decl);
        }
        Ok(())
    }

    /// This call ensures that a given offer for the given capability exists in this component's
    /// decl. If `self.component_loaded_from_pkg && !force_route` is true, we don't do anything.
    fn add_offer_for_capability(
        &mut self,
        capability: &frealmbuilder::Capability,
        offer_source: cm_rust::OfferSource,
        target_name: &str,
        force_route: bool,
    ) -> Result<(), Error> {
        if self.component_loaded_from_pkg && !force_route {
            // We don't modify package-local components unless force_route is true
            return Ok(());
        }
        if let cm_rust::OfferSource::Child(_) = &offer_source {
            if let frealmbuilder::Capability::Storage(_) = capability {
                return Err(Error::UnableToExpose("storage"));
            }
        }
        let capability_name = get_capability_name(&capability)?;

        let offer_decl = match &capability {
            frealmbuilder::Capability::Protocol(_) => {
                cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                    source: offer_source,
                    source_name: capability_name.clone().into(),
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: capability_name.into(),
                    dependency_type: cm_rust::DependencyType::Strong,
                })
            }
            frealmbuilder::Capability::Directory(_) => {
                cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                    source: offer_source,
                    source_name: capability_name.clone().into(),
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: capability_name.into(),
                    rights: None,
                    subdir: None,
                    dependency_type: cm_rust::DependencyType::Strong,
                })
            }
            frealmbuilder::Capability::Storage(_) => {
                cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                    source: offer_source,
                    source_name: capability_name.clone().into(),
                    target: cm_rust::OfferTarget::static_child(target_name.to_string()),
                    target_name: capability_name.into(),
                })
            }
            _ => return Err(Error::BadFidl),
        };
        if !self.decl.offers.contains(&offer_decl) {
            self.decl.offers.push(offer_decl);
        }
        Ok(())
    }
}

// TODO(77771): use the moniker crate once there's an id-free version of it.
#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord, Default)]
struct Moniker {
    path: Vec<String>,
}

impl From<&str> for Moniker {
    fn from(s: &str) -> Self {
        Moniker {
            path: match s {
                "" => vec![],
                _ => s.split('/').map(|s| s.to_string()).collect(),
            },
        }
    }
}

impl From<String> for Moniker {
    fn from(s: String) -> Self {
        s.as_str().into()
    }
}

impl From<Vec<String>> for Moniker {
    fn from(path: Vec<String>) -> Self {
        Moniker { path }
    }
}

impl TryFrom<frealmbuilder::RouteEndpoint> for Moniker {
    type Error = Error;

    fn try_from(route_endpoint: frealmbuilder::RouteEndpoint) -> Result<Self, Error> {
        match route_endpoint {
            frealmbuilder::RouteEndpoint::AboveRoot(_) => {
                panic!("tried to convert RouteEndpoint::AboveRoot into a moniker")
            }
            frealmbuilder::RouteEndpoint::Component(moniker) => Ok(moniker.into()),
            _ => Err(Error::BadFidl),
        }
    }
}

impl Display for Moniker {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.is_root() {
            write!(f, "<root of test realm>")
        } else {
            write!(f, "{}", self.path.join("/"))
        }
    }
}

impl Moniker {
    pub fn root() -> Self {
        Moniker { path: vec![] }
    }

    fn is_root(&self) -> bool {
        return self.path.is_empty();
    }

    fn child_name(&self) -> Option<&String> {
        self.path.last()
    }

    fn path(&self) -> &Vec<String> {
        &self.path
    }

    // If self is an ancestor of other_moniker, then returns the path to reach other_moniker from
    // self. Panics if self is not a parent of other_moniker.
    fn downward_path_to(&self, other_moniker: &Moniker) -> Vec<String> {
        let our_path = self.path.clone();
        let mut their_path = other_moniker.path.clone();
        for item in our_path {
            if Some(&item) != their_path.get(0) {
                panic!("downward_path_to called on non-ancestor moniker");
            }
            their_path.remove(0);
        }
        their_path
    }

    /// Returns the list of components comprised of this component's parent, then that component's
    /// parent, and so on. This list does not include the root component.
    ///
    /// For example, `"a/b/c/d".into().ancestry()` would return `vec!["a/b/c".into(), "a/b".into(),
    /// "a".into()]`
    fn ancestry(&self) -> Vec<Moniker> {
        let mut current_moniker = Moniker { path: vec![] };
        let mut res = vec![];
        let mut parent_path = self.path.clone();
        parent_path.pop();
        for part in parent_path {
            current_moniker.path.push(part.clone());
            res.push(current_moniker.clone());
        }
        res
    }

    fn parent(&self) -> Option<Self> {
        let mut path = self.path.clone();
        path.pop()?;
        Some(Moniker { path })
    }

    fn child(&self, child_name: String) -> Self {
        let mut path = self.path.clone();
        path.push(child_name);
        Moniker { path }
    }

    fn is_ancestor_of(&self, other_moniker: &Moniker) -> bool {
        if self.path.len() >= other_moniker.path.len() {
            return false;
        }
        for (element_from_us, element_from_them) in self.path.iter().zip(other_moniker.path.iter())
        {
            if element_from_us != element_from_them {
                return false;
            }
        }
        return true;
    }
}

fn is_relative_url(url: &str) -> bool {
    if url.len() == 0 || url.chars().nth(0) != Some('#') {
        return false;
    }
    if Url::parse(url) != Err(url::ParseError::RelativeUrlWithoutBase) {
        return false;
    }
    true
}

fn get_capability_name(capability: &frealmbuilder::Capability) -> Result<String, Error> {
    match &capability {
        frealmbuilder::Capability::Protocol(frealmbuilder::ProtocolCapability { name, .. }) => {
            Ok(name.as_ref().unwrap().clone())
        }
        frealmbuilder::Capability::Directory(frealmbuilder::DirectoryCapability {
            name, ..
        }) => Ok(name.as_ref().unwrap().clone()),
        frealmbuilder::Capability::Storage(frealmbuilder::StorageCapability { name, .. }) => {
            Ok(name.as_ref().unwrap().clone())
        }
        _ => Err(Error::BadFidl),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_io2 as fio2;

    #[fasync::run_singlethreaded(test)]
    async fn set_component() {
        let mut realm = RealmNode::default();

        let root_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let mut a_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("b".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };

        realm
            .set_component(
                Moniker::default(),
                frealmbuilder::Component::Decl(root_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(a_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();

        a_decl.children.push(cm_rust::ChildDecl {
            name: "b".to_string(),
            url: "fuchsia-pkg://b".to_string(),
            startup: fsys::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        });

        assert_eq!(
            realm.get_node_mut(&Moniker::default(), GetBehavior::ErrorIfMissing).unwrap().decl,
            root_decl
        );
        assert_eq!(
            realm.get_node_mut(&"a".into(), GetBehavior::ErrorIfMissing).unwrap().decl,
            a_decl
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn contains_component() {
        let mut realm = RealmNode::default();

        let root_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let a_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("b".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            children: vec![cm_rust::ChildDecl {
                name: "b".to_string(),
                url: "fuchsia-pkg://b".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            ..cm_rust::ComponentDecl::default()
        };

        realm
            .set_component(
                Moniker::default(),
                frealmbuilder::Component::Decl(root_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(a_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();

        assert_eq!(true, realm.contains(Moniker::default()));
        assert_eq!(true, realm.contains("a".into()));
        assert_eq!(true, realm.contains("a/b".into()));
        assert_eq!(false, realm.contains("a/a".into()));
        assert_eq!(false, realm.contains("b".into()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn mark_as_eager() {
        let mut realm = RealmNode::default();

        let root_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("a".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let a_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("b".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            ..cm_rust::ComponentDecl::default()
        };
        let b_decl = cm_rust::ComponentDecl {
            offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                source: cm_rust::OfferSource::Parent,
                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                target: cm_rust::OfferTarget::static_child("c".to_string()),
                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                dependency_type: cm_rust::DependencyType::Strong,
            })],
            children: vec![cm_rust::ChildDecl {
                name: "c".to_string(),
                url: "fuchsia-pkg://c".to_string(),
                startup: fsys::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            }],
            ..cm_rust::ComponentDecl::default()
        };

        realm
            .set_component(
                Moniker::default(),
                frealmbuilder::Component::Decl(root_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(a_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                frealmbuilder::Component::Decl(b_decl.clone().native_into_fidl()),
                &None,
            )
            .await
            .unwrap();

        realm.mark_as_eager("a/b/c".into()).unwrap();
        assert_eq!(
            realm.get_node_mut(&"a".into(), GetBehavior::ErrorIfMissing).unwrap().eager,
            true
        );
        assert_eq!(
            realm.get_node_mut(&"a/b".into(), GetBehavior::ErrorIfMissing).unwrap().decl.children,
            vec![cm_rust::ChildDecl {
                name: "c".to_string(),
                url: "fuchsia-pkg://c".to_string(),
                startup: fsys::StartupMode::Eager,
                environment: None,
                on_terminate: None,
            }]
        );
    }

    fn check_results(
        mut realm: RealmNode,
        expected_results: Vec<(&'static str, cm_rust::ComponentDecl)>,
    ) {
        assert!(!expected_results.is_empty(), "can't build an empty realm");

        for (component, decl) in expected_results {
            assert_eq!(
                realm
                    .get_node_mut(&component.into(), GetBehavior::ErrorIfMissing)
                    .expect("component is missing from realm")
                    .decl,
                decl,
                "decl in realm doesn't match expectations for component  {:?}",
                component
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn missing_route_source_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        let res = realm.route_capability(frealmbuilder::CapabilityRoute {
            capability: Some(frealmbuilder::Capability::Protocol(
                frealmbuilder::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..frealmbuilder::ProtocolCapability::EMPTY
                },
            )),
            source: Some(frealmbuilder::RouteEndpoint::Component("b".to_string())),
            targets: Some(vec![frealmbuilder::RouteEndpoint::Component("a".to_string())]),
            ..frealmbuilder::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::MissingRouteSource(m)) if m == "b".into() => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn empty_route_targets() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        let res = realm.route_capability(frealmbuilder::CapabilityRoute {
            capability: Some(frealmbuilder::Capability::Protocol(
                frealmbuilder::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..frealmbuilder::ProtocolCapability::EMPTY
                },
            )),
            source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![]),
            ..frealmbuilder::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => {
                if let Error::RouteTargetsEmpty = e {
                    ()
                } else {
                    panic!("unexpected error: {:?}", e);
                }
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn multiple_offer_same_source() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "1/src".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "2/target_1".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "2/target_2".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://c".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("1/src".to_string())),
                targets: Some(vec![
                    frealmbuilder::RouteEndpoint::Component("2/target_1".to_string()),
                    frealmbuilder::RouteEndpoint::Component("2/target_2".to_string()),
                ]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn same_capability_from_different_sources_in_same_node_error() {
        {
            let mut realm = RealmNode::default();
            realm
                .set_component(
                    "1/a".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "1/b".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/c".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://c".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/d".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://d".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .route_capability(frealmbuilder::CapabilityRoute {
                    capability: Some(frealmbuilder::Capability::Protocol(
                        frealmbuilder::ProtocolCapability {
                            name: Some("fidl.examples.routing.echo.Echo".to_string()),
                            ..frealmbuilder::ProtocolCapability::EMPTY
                        },
                    )),
                    source: Some(frealmbuilder::RouteEndpoint::Component("1/a".to_string())),
                    targets: Some(vec![frealmbuilder::RouteEndpoint::Component("2/c".to_string())]),
                    ..frealmbuilder::CapabilityRoute::EMPTY
                })
                .unwrap();
            realm
                .route_capability(frealmbuilder::CapabilityRoute {
                    capability: Some(frealmbuilder::Capability::Protocol(
                        frealmbuilder::ProtocolCapability {
                            name: Some("fidl.examples.routing.echo.Echo".to_string()),
                            ..frealmbuilder::ProtocolCapability::EMPTY
                        },
                    )),
                    source: Some(frealmbuilder::RouteEndpoint::Component("1/b".to_string())),
                    targets: Some(vec![frealmbuilder::RouteEndpoint::Component("2/d".to_string())]),
                    ..frealmbuilder::CapabilityRoute::EMPTY
                })
                .unwrap();
            // get and set this component, to confirm that `set_component` runs `validate`
            let decl = realm.get_component_decl("1".into()).unwrap().native_into_fidl();
            let res =
                realm.set_component("1".into(), frealmbuilder::Component::Decl(decl), &None).await;

            match res {
                Err(Error::ValidationError(_, e)) => {
                    assert_eq!(
                        e,
                        cm_fidl_validator::ErrorList {
                            errs: vec![cm_fidl_validator::Error::DuplicateField(
                                cm_fidl_validator::DeclField {
                                    decl: "ExposeProtocolDecl".to_string(),
                                    field: "target_name".to_string()
                                },
                                "fidl.examples.routing.echo.Echo".to_string()
                            )]
                        }
                    );
                }
                Err(e) => panic!("unexpected error: {:?}", e),
                Ok(_) => panic!("builder commands should have errored"),
            }
        }

        {
            let mut realm = RealmNode::default();
            realm
                .set_component(
                    "1/a".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "1/b".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/c".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://c".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .set_component(
                    "2/d".into(),
                    frealmbuilder::Component::Url("fuchsia-pkg://d".to_string()),
                    &None,
                )
                .await
                .unwrap();
            realm
                .route_capability(frealmbuilder::CapabilityRoute {
                    capability: Some(frealmbuilder::Capability::Protocol(
                        frealmbuilder::ProtocolCapability {
                            name: Some("fidl.examples.routing.echo.Echo".to_string()),
                            ..frealmbuilder::ProtocolCapability::EMPTY
                        },
                    )),
                    source: Some(frealmbuilder::RouteEndpoint::Component("1/a".to_string())),
                    targets: Some(vec![frealmbuilder::RouteEndpoint::Component("1/b".to_string())]),
                    ..frealmbuilder::CapabilityRoute::EMPTY
                })
                .unwrap();
            realm
                .route_capability(frealmbuilder::CapabilityRoute {
                    capability: Some(frealmbuilder::Capability::Protocol(
                        frealmbuilder::ProtocolCapability {
                            name: Some("fidl.examples.routing.echo.Echo".to_string()),
                            ..frealmbuilder::ProtocolCapability::EMPTY
                        },
                    )),
                    source: Some(frealmbuilder::RouteEndpoint::Component("2/c".to_string())),
                    targets: Some(vec![frealmbuilder::RouteEndpoint::Component("2/d".to_string())]),
                    ..frealmbuilder::CapabilityRoute::EMPTY
                })
                .unwrap();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn missing_route_target_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        let res = realm.route_capability(frealmbuilder::CapabilityRoute {
            capability: Some(frealmbuilder::Capability::Protocol(
                frealmbuilder::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..frealmbuilder::ProtocolCapability::EMPTY
                },
            )),
            source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![frealmbuilder::RouteEndpoint::Component("b".to_string())]),
            ..frealmbuilder::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::MissingRouteTarget(m)) => {
                assert_eq!(m, "b".into());
            }
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[test]
    fn route_source_and_target_both_above_root_error() {
        let mut realm = RealmNode::default();
        let res = realm.route_capability(frealmbuilder::CapabilityRoute {
            capability: Some(frealmbuilder::Capability::Protocol(
                frealmbuilder::ProtocolCapability {
                    name: Some("fidl.examples.routing.echo.Echo".to_string()),
                    ..frealmbuilder::ProtocolCapability::EMPTY
                },
            )),
            source: Some(frealmbuilder::RouteEndpoint::AboveRoot(frealmbuilder::AboveRoot {})),
            targets: Some(vec![frealmbuilder::RouteEndpoint::AboveRoot(
                frealmbuilder::AboveRoot {},
            )]),
            ..frealmbuilder::CapabilityRoute::EMPTY
        });

        match res {
            Err(Error::RouteSourceAndTargetMatch(frealmbuilder::RouteEndpoint::AboveRoot(
                frealmbuilder::AboveRoot {},
            ))) => (),
            Ok(_) => panic!("builder commands should have errored"),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn expose_storage_from_child_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        let res = realm.route_capability(frealmbuilder::CapabilityRoute {
            capability: Some(frealmbuilder::Capability::Storage(
                frealmbuilder::StorageCapability {
                    name: Some("foo".to_string()),
                    path: Some("foo".to_string()),
                    ..frealmbuilder::StorageCapability::EMPTY
                },
            )),
            source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![frealmbuilder::RouteEndpoint::AboveRoot(
                frealmbuilder::AboveRoot {},
            )]),
            ..frealmbuilder::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::UnableToExpose("storage")) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn offer_storage_from_child_error() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        let res = realm.route_capability(frealmbuilder::CapabilityRoute {
            capability: Some(frealmbuilder::Capability::Storage(
                frealmbuilder::StorageCapability {
                    name: Some("foo".to_string()),
                    path: Some("/foo".to_string()),
                    ..frealmbuilder::StorageCapability::EMPTY
                },
            )),
            source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
            targets: Some(vec![frealmbuilder::RouteEndpoint::Component("b".to_string())]),
            ..frealmbuilder::CapabilityRoute::EMPTY
        });

        match res {
            Ok(_) => panic!("builder commands should have errored"),
            Err(Error::UnableToExpose("storage")) => (),
            Err(e) => panic!("unexpected error: {:?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_storage_routing() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Storage(
                    frealmbuilder::StorageCapability {
                        name: Some("foo".to_string()),
                        path: Some("/bar".to_string()),
                        ..frealmbuilder::StorageCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::AboveRoot(frealmbuilder::AboveRoot {})),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("a".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![cm_rust::OfferDecl::Storage(cm_rust::OfferStorageDecl {
                            source: cm_rust::OfferSource::Parent,
                            source_name: "foo".into(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "foo".into(),
                        })],
                        children: vec![
                                // Mock children aren't inserted into the decls at this point, as their
                                // URLs are unknown until registration with the framework intermediary,
                                // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Storage(cm_rust::UseStorageDecl {
                            source_name: "foo".into(),
                            target_path: "/bar".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn two_sibling_realm_no_mocks() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("b".into()).unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("b".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![(
                "",
                cm_rust::ComponentDecl {
                    offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                        source: cm_rust::OfferSource::static_child("a".to_string()),
                        source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        target: cm_rust::OfferTarget::static_child("b".to_string()),
                        target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        dependency_type: cm_rust::DependencyType::Strong,
                    })],
                    children: vec![
                        cm_rust::ChildDecl {
                            name: "a".to_string(),
                            url: "fuchsia-pkg://a".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        },
                        cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                    ],
                    ..cm_rust::ComponentDecl::default()
                },
            )],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn two_sibling_realm_both_mocks() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("b".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::static_child("a".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("b".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "b",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Parent,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn mock_with_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("a/b".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        offers: vec![cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("b".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn three_sibling_realm_one_mock() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "c".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://c".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("c".into()).unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("b".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Directory(
                    frealmbuilder::DirectoryCapability {
                        name: Some("example-dir".to_string()),
                        path: Some("/example".to_string()),
                        rights: Some(fio2::RW_STAR_DIR),
                        ..frealmbuilder::DirectoryCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("b".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("c".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![
                            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("b".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                                source: cm_rust::OfferSource::static_child("b".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("c".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                            cm_rust::ChildDecl {
                                name: "a".to_string(),
                                url: "fuchsia-pkg://a".to_string(),
                                startup: fsys::StartupMode::Lazy,
                                environment: None,
                                on_terminate: None,
                            },
                            cm_rust::ChildDecl {
                                name: "c".to_string(),
                                url: "fuchsia-pkg://c".to_string(),
                                startup: fsys::StartupMode::Eager,
                                environment: None,
                                on_terminate: None,
                            },
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "b",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Parent,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        capabilities: vec![cm_rust::CapabilityDecl::Directory(
                            cm_rust::DirectoryDecl {
                                name: "example-dir".try_into().unwrap(),
                                source_path: Some("/example".try_into().unwrap()),
                                rights: fio2::RW_STAR_DIR,
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Directory(
                            cm_rust::ExposeDirectoryDecl {
                                source: cm_rust::ExposeSource::Self_,
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::ExposeTarget::Parent,
                                target_name: "example-dir".try_into().unwrap(),
                                rights: None,
                                subdir: None,
                            },
                        )],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn three_siblings_two_targets() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "b".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "c".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://c".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("a".into()).unwrap();
        realm.mark_as_eager("c".into()).unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("b".to_string())),
                targets: Some(vec![
                    frealmbuilder::RouteEndpoint::Component("a".to_string()),
                    frealmbuilder::RouteEndpoint::Component("c".to_string()),
                ]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Directory(
                    frealmbuilder::DirectoryCapability {
                        name: Some("example-dir".to_string()),
                        path: Some("/example".to_string()),
                        rights: Some(fio2::RW_STAR_DIR),
                        ..frealmbuilder::DirectoryCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("b".to_string())),
                targets: Some(vec![
                    frealmbuilder::RouteEndpoint::Component("a".to_string()),
                    frealmbuilder::RouteEndpoint::Component("c".to_string()),
                ]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![(
                "",
                cm_rust::ComponentDecl {
                    offers: vec![
                        cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        }),
                        cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("c".to_string()),
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        }),
                        cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("a".to_string()),
                            target_name: "example-dir".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                            rights: None,
                            subdir: None,
                        }),
                        cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                            source: cm_rust::OfferSource::static_child("b".to_string()),
                            source_name: "example-dir".try_into().unwrap(),
                            target: cm_rust::OfferTarget::static_child("c".to_string()),
                            target_name: "example-dir".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                            rights: None,
                            subdir: None,
                        }),
                    ],
                    children: vec![
                        cm_rust::ChildDecl {
                            name: "a".to_string(),
                            url: "fuchsia-pkg://a".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                        cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        },
                        cm_rust::ChildDecl {
                            name: "c".to_string(),
                            url: "fuchsia-pkg://c".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        },
                    ],
                    ..cm_rust::ComponentDecl::default()
                },
            )],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn two_cousins_realm_one_mock() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a/b".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://a-b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "c/d".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("c/d".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Directory(
                    frealmbuilder::DirectoryCapability {
                        name: Some("example-dir".to_string()),
                        path: Some("/example".to_string()),
                        rights: Some(fio2::RW_STAR_DIR),
                        ..frealmbuilder::DirectoryCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("c/d".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        offers: vec![
                            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("c".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                                source: cm_rust::OfferSource::static_child("a".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("c".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        children: vec![
                            // Generated children aren't inserted into the decls at this point, as
                            // their URLs are unknown until registration with the framework
                            // intermediary, and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        exposes: vec![
                            cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                                source: cm_rust::ExposeSource::Child("b".to_string()),
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::ExposeTarget::Parent,
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            }),
                            cm_rust::ExposeDecl::Directory(cm_rust::ExposeDirectoryDecl {
                                source: cm_rust::ExposeSource::Child("b".to_string()),
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::ExposeTarget::Parent,
                                target_name: "example-dir".try_into().unwrap(),
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://a-b".to_string(),
                            startup: fsys::StartupMode::Lazy,
                            environment: None,
                            on_terminate: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "c",
                    cm_rust::ComponentDecl {
                        offers: vec![
                            cm_rust::OfferDecl::Protocol(cm_rust::OfferProtocolDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("d".to_string()),
                                target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::OfferDecl::Directory(cm_rust::OfferDirectoryDecl {
                                source: cm_rust::OfferSource::Parent,
                                source_name: "example-dir".try_into().unwrap(),
                                target: cm_rust::OfferTarget::static_child("d".to_string()),
                                target_name: "example-dir".try_into().unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                                rights: None,
                                subdir: None,
                            }),
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "c/d",
                    cm_rust::ComponentDecl {
                        uses: vec![
                            cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                                source: cm_rust::UseSource::Parent,
                                source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                target_path: "/svc/fidl.examples.routing.echo.Echo"
                                    .try_into()
                                    .unwrap(),
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                            cm_rust::UseDecl::Directory(cm_rust::UseDirectoryDecl {
                                source: cm_rust::UseSource::Parent,
                                source_name: "example-dir".try_into().unwrap(),
                                target_path: "/example".try_into().unwrap(),
                                rights: fio2::RW_STAR_DIR,
                                subdir: None,
                                dependency_type: cm_rust::DependencyType::Strong,
                            }),
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn parent_use_from_url_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                frealmbuilder::Component::Url("fuchsia-pkg://b".to_string()),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("a/b".into()).unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("a".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                                // Mock children aren't inserted into the decls at this point, as their
                                // URLs are unknown until registration with the framework intermediary,
                                // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![cm_rust::ChildDecl {
                            name: "b".to_string(),
                            url: "fuchsia-pkg://b".to_string(),
                            startup: fsys::StartupMode::Eager,
                            environment: None,
                            on_terminate: None,
                        }],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn parent_use_from_mock_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm
            .set_component(
                "a/b".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("a/b".into()).unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a/b".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("a".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a/b",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn grandparent_use_from_mock_child() {
        let mut realm = RealmNode::default();
        realm
            .set_component(
                "a/b/c".into(),
                frealmbuilder::Component::Decl(
                    cm_rust::ComponentDecl::default().native_into_fidl(),
                ),
                &None,
            )
            .await
            .unwrap();
        realm.mark_as_eager("a/b/c".into()).unwrap();
        realm
            .route_capability(frealmbuilder::CapabilityRoute {
                capability: Some(frealmbuilder::Capability::Protocol(
                    frealmbuilder::ProtocolCapability {
                        name: Some("fidl.examples.routing.echo.Echo".to_string()),
                        ..frealmbuilder::ProtocolCapability::EMPTY
                    },
                )),
                source: Some(frealmbuilder::RouteEndpoint::Component("a/b/c".to_string())),
                targets: Some(vec![frealmbuilder::RouteEndpoint::Component("a".to_string())]),
                ..frealmbuilder::CapabilityRoute::EMPTY
            })
            .unwrap();

        check_results(
            realm,
            vec![
                (
                    "",
                    cm_rust::ComponentDecl {
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a",
                    cm_rust::ComponentDecl {
                        uses: vec![cm_rust::UseDecl::Protocol(cm_rust::UseProtocolDecl {
                            source: cm_rust::UseSource::Child("b".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target_path: "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            dependency_type: cm_rust::DependencyType::Strong,
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a/b",
                    cm_rust::ComponentDecl {
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Child("c".to_string()),
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        children: vec![
                            // Mock children aren't inserted into the decls at this point, as their
                            // URLs are unknown until registration with the framework intermediary,
                            // and that happens during Realm::create
                        ],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
                (
                    "a/b/c",
                    cm_rust::ComponentDecl {
                        capabilities: vec![cm_rust::CapabilityDecl::Protocol(
                            cm_rust::ProtocolDecl {
                                name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                source_path: Some(
                                    "/svc/fidl.examples.routing.echo.Echo".try_into().unwrap(),
                                ),
                            },
                        )],
                        exposes: vec![cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
                            source: cm_rust::ExposeSource::Self_,
                            source_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                            target: cm_rust::ExposeTarget::Parent,
                            target_name: "fidl.examples.routing.echo.Echo".try_into().unwrap(),
                        })],
                        ..cm_rust::ComponentDecl::default()
                    },
                ),
            ],
        );
    }
}
