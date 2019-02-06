// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod moniker;
mod resolver;
mod runner;

pub use self::{moniker::*, resolver::*, runner::*};
use {
    crate::{data, io_util},
    crate::ns_util::PKG_PATH,
    failure::{format_err, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys,
    futures::lock::Mutex,
    std::{cell::RefCell, error, fmt, rc::Rc},
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
    pub root_default_runner: Box<dyn Runner>,
}

/// The component model holds authoritative state about a tree of component instances, including
/// each instance's identity, lifecycle, capabilities, and topological relationships.  It also
/// provides operations for instantiating, destroying, querying, and controlling component
/// instances at runtime.
///
/// To facilitate unit testing, the component model does not directly perform IPC.  Instead, it
/// delegates external interfacing concerns to other objects that implement traits such as
/// `Runner` and `Resolver`.
pub struct Model {
    root_realm: Rc<RefCell<Realm>>,
}

/// A realm is a container for an individual component instance and its children.  It is provided
/// by the parent of the instance or by the component manager itself in the case of the root realm.
///
/// The realm's properties influence the runtime behavior of the subtree of component instances
/// that it contains, including component resolution, execution, and service discovery.
struct Realm {
    /// The registry for resolving component URIs within the realm.
    resolver_registry: Rc<ResolverRegistry>,
    /// The default runner (nominally runs ELF binaries) for executing components
    /// within the realm that do not explicitly specify a runner.
    default_runner: Rc<Box<dyn Runner>>,
    /// The component that has been instantiated within the realm.
    instance: Instance,
}

/// An instance of a component.
/// TODO: Describe child instances (map of child moniker to realm).
struct Instance {
    /// The component's URI.
    /// The URI is only meaningful
    component_uri: String,
    /// Execution state for the component instance or `None` if not running.
    execution: Mutex<Option<Execution>>,
}

/// The execution state for a component instance that has started running.
// TODO: Hold the component instance's controller.
struct Execution {
    component: fsys::Component,
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            root_realm: Rc::new(RefCell::new(Realm {
                resolver_registry: Rc::new(params.root_resolver_registry),
                default_runner: Rc::new(params.root_default_runner),
                instance: Instance {
                    component_uri: params.root_component_uri,
                    execution: Mutex::new(None),
                },
            })),
        }
    }

    /// Binds to the component instance with the specified moniker, causing it to start if it is
    /// not already running.
    pub async fn bind_instance(&self, moniker: AbsoluteMoniker) -> Result<(), ModelError> {
        // TODO: Use moniker to locate non-root component instances.
        if moniker.is_root() {
            await!(self.bind_instance_in_realm(self.root_realm.clone()))
        } else {
            Err(ModelError::InstanceNotFound)
        }
    }

    async fn bind_instance_in_realm(
        &self,
        realm_cell: Rc<RefCell<Realm>>,
    ) -> Result<(), ModelError> {
        // There can only be one task manipulating an instance's execution at a time.
        let realm = realm_cell.borrow();
        let mut execution_lock = await!(realm.instance.execution.lock());
        match &*execution_lock {
            Some(_) => Ok(()),
            None => {
                let component =
                    await!(realm.resolver_registry.resolve(&realm.instance.component_uri))?;
                let (component, ns) = Self::ns_from_component(component)?;
                let start_info = fsys::ComponentStartInfo {
                    resolved_uri: component.resolved_uri.clone(),
                    program: component
                        .decl
                        .as_ref()
                        .and_then(|x| data::clone_option_dictionary(&x.program)),
                    ns: Some(ns),
                };
                await!(realm.default_runner.start(start_info))?;
                *execution_lock = Some(Execution { component });
                Ok(())
            }
        }
    }

    // Takes ownership of `component` and returns it unchanged. We must take ownership to be able
    // to clone the package directory.
    fn ns_from_component(
        component: fsys::Component,
    ) -> Result<(fsys::Component, fsys::ComponentNamespace), ModelError> {
        // TODO: Populate namespace from the component declaration.
        let mut ns = fsys::ComponentNamespace { paths: vec![], directories: vec![] };
        let mut out_component = fsys::Component {
            resolved_uri: component.resolved_uri,
            decl: component.decl,
            package: None,
        };
        if let Some(package) = component.package {
            if package.package_dir.is_none() {
                return Err(ResolverError::internal_error("Package missing package_dir").into());
            }
            let (package_dir, cloned_dir) =
                Self::clone_dir(package.package_dir.unwrap()).map_err(|e| {
                    ModelError::from(ResolverError::internal_error(format!(
                        "failed to clone package_dir: {}",
                        e
                    )))
                })?;
            ns.paths.push(PKG_PATH.to_str().unwrap().to_string());
            ns.directories.push(cloned_dir);
            out_component.package = Some(fsys::Package {
                package_uri: package.package_uri,
                package_dir: Some(package_dir),
            });
        }
        Ok((out_component, ns))
    }

    fn clone_dir(
        dir: ClientEnd<DirectoryMarker>,
    ) -> Result<(ClientEnd<DirectoryMarker>, ClientEnd<DirectoryMarker>), Error> {
        let dir_proxy = dir.into_proxy()?;
        let clone_end = ClientEnd::new(
            io_util::clone_directory(&dir_proxy)?
                .into_channel()
                .map_err(|_| format_err!("could not convert directory into channel"))?
                .into_zx_channel(),
        );
        let orig_end = ClientEnd::new(dir_proxy.into_channel().unwrap().into_zx_channel());
        Ok((orig_end, clone_end))
    }
}

/// Errors produced by `Model`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ModelError {
    InstanceNotFound,
    ResolverError(ResolverError),
    RunnerError(RunnerError),
}

impl fmt::Display for ModelError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            ModelError::InstanceNotFound => write!(f, "component instance not found"),
            ModelError::ResolverError(err) => err.fmt(f),
            ModelError::RunnerError(err) => err.fmt(f),
        }
    }
}

impl error::Error for ModelError {
    fn source(&self) -> Option<&(dyn error::Error + 'static)> {
        match self {
            ModelError::InstanceNotFound => None,
            ModelError::ResolverError(err) => err.source(),
            ModelError::RunnerError(err) => err.source(),
        }
    }
}

impl From<ResolverError> for ModelError {
    fn from(err: ResolverError) -> Self {
        ModelError::ResolverError(err)
    }
}

impl From<RunnerError> for ModelError {
    fn from(err: RunnerError) -> Self {
        ModelError::RunnerError(err)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::future::{self, FutureObj};

    struct MockResolver {}

    impl Resolver for MockResolver {
        fn resolve(
            &self,
            component_uri: &str,
        ) -> FutureObj<Result<fsys::Component, ResolverError>> {
            assert_eq!("test:///root", component_uri);
            FutureObj::new(Box::new(future::ok(fsys::Component {
                resolved_uri: Some("test:///resolved_root".to_string()),
                decl: Some(fsys::ComponentDecl {
                    program: None,
                    uses: None,
                    exposes: None,
                    offers: None,
                    facets: None,
                    children: None,
                }),
                package: None,
            })))
        }
    }

    struct MockRunner {}

    impl Runner for MockRunner {
        fn start(
            &self,
            start_info: fsys::ComponentStartInfo,
        ) -> FutureObj<Result<(), RunnerError>> {
            assert_eq!(Some("test:///resolved_root".to_string()), start_info.resolved_uri);
            FutureObj::new(Box::new(future::ok(())))
        }
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn bind_instance_non_existent() {
        let resolver = ResolverRegistry::new();
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(MockRunner {}),
        });
        assert_eq!(
            Err(ModelError::InstanceNotFound),
            await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
                "no-such-instance".to_string()
            )])))
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn bind_instance_successfully() {
        let mut resolver = ResolverRegistry::new();
        resolver.register("test".to_string(), Box::new(MockResolver {}));
        let model = Model::new(ModelParams {
            root_component_uri: "test:///root".to_string(),
            root_resolver_registry: resolver,
            root_default_runner: Box::new(MockRunner {}),
        });
        assert_eq!(Ok(()), await!(model.bind_instance(AbsoluteMoniker::root())));
    }
}
