// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod moniker;
mod resolver;
mod runner;

pub use self::{moniker::*, resolver::*, runner::*};
use {
    crate::ns_util::PKG_PATH,
    crate::{data, io_util},
    failure::{Error, Fail},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys,
    futures::lock::Mutex,
    std::{cell::RefCell, convert::TryFrom, rc::Rc},
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
    resolved_uri: String,
    decl: fsys::ComponentDecl,
    package_dir: Option<DirectoryProxy>,
}

impl TryFrom<fsys::Component> for Execution {
    type Error = ModelError;

    fn try_from(component: fsys::Component) -> Result<Self, Self::Error> {
        if component.resolved_uri.is_none() || component.decl.is_none() {
            return Err(ModelError::ComponentInvalid);
        }
        let package_dir = match component.package {
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
        Ok(Execution {
            resolved_uri: component.resolved_uri.unwrap(),
            decl: component.decl.unwrap(),
            package_dir,
        })
    }
}

impl Execution {
    fn make_namespace(&self) -> Result<fsys::ComponentNamespace, Error> {
        // TODO: Populate namespace from the component declaration.
        let mut ns = fsys::ComponentNamespace { paths: vec![], directories: vec![] };
        if let Some(package_dir) = self.package_dir.as_ref() {
            let cloned_dir = ClientEnd::new(
                io_util::clone_directory(package_dir)?
                    .into_channel()
                    .expect("could not convert directory to channel")
                    .into_zx_channel(),
            );
            ns.paths.push(PKG_PATH.to_str().unwrap().to_string());
            ns.directories.push(cloned_dir);
        }
        Ok(ns)
    }
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
            Err(ModelError::instance_not_found(moniker))
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
                let execution = Execution::try_from(component)?;
                let ns = execution
                    .make_namespace()
                    .map_err(|e| ModelError::namespace_creation_failed(e))?;
                let start_info = fsys::ComponentStartInfo {
                    resolved_uri: Some(execution.resolved_uri.clone()),
                    program: data::clone_option_dictionary(&execution.decl.program),
                    ns: Some(ns),
                };
                await!(realm.default_runner.start(start_info))?;
                *execution_lock = Some(execution);
                Ok(())
            }
        }
    }
}

// TODO: Derive from Fail and take cause where appropriate.
/// Errors produced by `Model`.
#[derive(Debug, Fail)]
pub enum ModelError {
    #[fail(display = "component instance not found with moniker {}", moniker)]
    InstanceNotFound { moniker: AbsoluteMoniker },
    #[fail(display = "component declaration invalid")]
    ComponentInvalid,
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
}

impl ModelError {
    fn instance_not_found(moniker: AbsoluteMoniker) -> ModelError {
        ModelError::InstanceNotFound { moniker }
    }

    fn namespace_creation_failed(err: impl Into<Error>) -> ModelError {
        ModelError::NamespaceCreationFailed { err: err.into() }
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
        let expected_res: Result<(), ModelError> = Err(ModelError::instance_not_found(
            AbsoluteMoniker::new(vec![ChildMoniker::new("no-such-instance".to_string())]),
        ));
        assert_eq!(
            format!("{:?}", expected_res,),
            format!(
                "{:?}",
                await!(model.bind_instance(AbsoluteMoniker::new(vec![ChildMoniker::new(
                    "no-such-instance".to_string()
                )])))
            ),
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
        let expected_res: Result<(), ModelError> = Ok(());
        assert_eq!(
            format!("{:?}", expected_res),
            format!("{:?}", await!(model.bind_instance(AbsoluteMoniker::root()))),
        );
    }
}
