// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        realm::{Realm, WeakRealm},
        resolver::{Resolver, ResolverError, ResolverFut, ResolverRegistry},
    },
    fidl_fuchsia_sys2 as fsys,
    std::{collections::HashMap, sync::Arc, time::Duration},
    thiserror::Error,
};

/// A realm's environment, populated from a component's [`EnvironmentDecl`].
/// An environment defines intrinsic behaviors of a component's realm. Components
/// can define an environment, but do not interact with it directly.
///
/// [`EnvironmentDecl`]: fidl_fuchsia_sys2::EnvironmentDecl
pub struct Environment {
    /// The parent that created or inherited the environment.
    parent: Option<WeakRealm>,
    /// The extension mode of this environment.
    extends: EnvironmentExtends,
    /// The runners available in this environment.
    runner_registry: RunnerRegistry,
    /// The resolvers in this environment, mapped to URL schemes.
    resolver_registry: ResolverRegistry,
    /// The deadline for runners to respond to `ComponentController.Stop` calls.
    stop_timeout: Duration,
}

pub const DEFAULT_STOP_TIMEOUT: Duration = Duration::from_secs(5);

#[derive(Debug, Error, Clone)]
pub enum EnvironmentError {
    #[error(
        "stop timeout could not be set, environment has no parent and does not specify a value"
    )]
    StopTimeoutUnknown,
}

/// How this environment extends its parent's.
#[derive(Debug, Clone)]
pub enum EnvironmentExtends {
    /// This environment extends the environment of its parent's.
    Realm,
    /// This environment was created from scratch.
    None,
}

impl From<fsys::EnvironmentExtends> for EnvironmentExtends {
    fn from(e: fsys::EnvironmentExtends) -> Self {
        match e {
            fsys::EnvironmentExtends::Realm => Self::Realm,
            fsys::EnvironmentExtends::None => Self::None,
        }
    }
}

impl Environment {
    /// Creates a new empty environment without a parent.
    pub fn empty() -> Environment {
        Environment {
            parent: None,
            extends: EnvironmentExtends::None,
            runner_registry: RunnerRegistry::default(),
            resolver_registry: ResolverRegistry::new(),
            stop_timeout: DEFAULT_STOP_TIMEOUT,
        }
    }

    /// Creates a new root environment with a resolver registry and no parent.
    pub fn new_root(
        runner_registry: RunnerRegistry,
        resolver_registry: ResolverRegistry,
    ) -> Environment {
        Environment {
            parent: None,
            extends: EnvironmentExtends::None,
            runner_registry,
            resolver_registry,
            stop_timeout: DEFAULT_STOP_TIMEOUT,
        }
    }

    /// Creates an environment from `env_decl`, using `parent` as the parent realm.
    pub fn from_decl(
        parent: &Arc<Realm>,
        env_decl: &cm_rust::EnvironmentDecl,
    ) -> Result<Environment, EnvironmentError> {
        Ok(Environment {
            parent: Some(parent.into()),
            extends: env_decl.extends.into(),
            runner_registry: RunnerRegistry::from_decl(&env_decl.runners),
            resolver_registry: ResolverRegistry::new(),
            stop_timeout: match env_decl.stop_timeout_ms {
                Some(timeout) => Duration::from_millis(timeout.into()),
                None => match env_decl.extends {
                    fsys::EnvironmentExtends::Realm => parent.environment.stop_timeout(),
                    fsys::EnvironmentExtends::None => {
                        return Err(EnvironmentError::StopTimeoutUnknown);
                    }
                },
            },
        })
    }

    /// Creates a new environment with `parent` as the parent.
    pub fn new_inheriting(parent: &Arc<Realm>) -> Environment {
        Environment {
            parent: Some(parent.into()),
            extends: EnvironmentExtends::Realm,
            runner_registry: RunnerRegistry::default(),
            resolver_registry: ResolverRegistry::new(),
            stop_timeout: parent.environment.stop_timeout(),
        }
    }

    pub fn stop_timeout(&self) -> Duration {
        self.stop_timeout
    }

    /// Returns the runner registered to `name` and the realm that created the environment the
    /// runner was registered to (`None` for component manager's realm). Returns `None` if there
    /// was no match.
    pub fn get_registered_runner(
        &self,
        name: &cm_rust::CapabilityName,
    ) -> Result<Option<(Option<Arc<Realm>>, RunnerRegistration)>, ModelError> {
        let parent = self.parent.as_ref().map(|p| p.upgrade()).transpose()?;
        match self.runner_registry.get_runner(name) {
            Some(reg) => Ok(Some((parent, reg.clone()))),
            None => match self.extends {
                EnvironmentExtends::Realm => {
                    parent.unwrap().environment.get_registered_runner(name)
                }
                EnvironmentExtends::None => {
                    return Ok(None);
                }
            },
        }
    }
}

impl Resolver for Environment {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
        Box::pin(async move {
            match self.resolver_registry.resolve(component_url).await {
                Err(ResolverError::SchemeNotRegistered) => match &self.extends {
                    EnvironmentExtends::Realm => {
                        self.parent
                            .as_ref()
                            .unwrap()
                            .upgrade()
                            .map_err(|_| ResolverError::SchemeNotRegistered)?
                            .environment
                            .resolve(component_url)
                            .await
                    }
                    EnvironmentExtends::None => Err(ResolverError::SchemeNotRegistered),
                },
                result => result,
            }
        })
    }
}

/// The set of runners available in a realm's environment.
///
/// [`RunnerRegistration`]: fidl_fuchsia_sys2::RunnerRegistration
pub struct RunnerRegistry {
    runners: HashMap<cm_rust::CapabilityName, RunnerRegistration>,
}

impl RunnerRegistry {
    pub fn default() -> Self {
        Self { runners: HashMap::new() }
    }

    pub fn new(runners: HashMap<cm_rust::CapabilityName, RunnerRegistration>) -> Self {
        Self { runners }
    }

    pub fn from_decl(regs: &Vec<cm_rust::RunnerRegistration>) -> Self {
        let mut runners = HashMap::new();
        for reg in regs {
            runners.insert(
                reg.target_name.clone(),
                RunnerRegistration {
                    source_name: reg.source_name.clone(),
                    source: reg.source.clone(),
                },
            );
        }
        Self { runners }
    }
    pub fn get_runner(&self, name: &cm_rust::CapabilityName) -> Option<&RunnerRegistration> {
        self.runners.get(name)
    }
}

/// A single runner registered in an environment.
///
/// [`RunnerRegistration`]: fidl_fuchsia_sys2::RunnerRegistration
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RunnerRegistration {
    pub source: cm_rust::RegistrationSource,
    pub source_name: cm_rust::CapabilityName,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            binding::Binder,
            error::ModelError,
            model::{Model, ModelParams},
            moniker::AbsoluteMoniker,
            realm::BindReason,
            testing::{
                mocks::MockResolver,
                test_helpers::{
                    ChildDeclBuilder, CollectionDeclBuilder, ComponentDeclBuilder,
                    EnvironmentDeclBuilder,
                },
            },
        },
        fuchsia_async as fasync,
        maplit::hashmap,
        matches::assert_matches,
        std::sync::Weak,
    };

    #[test]
    fn test_from_decl() {
        let realm = Arc::new(Realm::new_root_realm(
            Environment::empty(),
            Weak::new(),
            "test:///root".to_string(),
        ));
        let environment = Environment::from_decl(
            &realm,
            &EnvironmentDeclBuilder::new()
                .name("env")
                .extends(fsys::EnvironmentExtends::None)
                .stop_timeout(1234)
                .build(),
        )
        .expect("environment construction failed");
        assert_matches!(environment.parent, Some(_));

        let environment = Environment::from_decl(
            &realm,
            &EnvironmentDeclBuilder::new()
                .name("env")
                .extends(fsys::EnvironmentExtends::Realm)
                .build(),
        )
        .expect("environment constuction failed");
        assert_matches!(environment.parent, Some(_));
    }

    // Each component declares an environment for their child that inherits from the realm's
    // environment. The leaf component should be able to access the resolvers of the root realm.
    #[fasync::run_singlethreaded(test)]
    async fn test_inherit_root() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: cm_rust::RegistrationSource::Parent,
            source_name: "test-src".into(),
        };
        let runners: HashMap<cm_rust::CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let mut resolver = MockResolver::new();
        resolver.add_component(
            "root",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("a").url("test:///a").environment("env_a"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_a")
                        .extends(fsys::EnvironmentExtends::Realm),
                )
                .build(),
        );
        resolver.add_component(
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("b").url("test:///b").environment("env_b"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_b")
                        .extends(fsys::EnvironmentExtends::Realm),
                )
                .build(),
        );
        resolver.add_component("b", ComponentDeclBuilder::new_empty_component().build());
        let resolvers = {
            let mut registry = ResolverRegistry::new();
            registry.register("test".to_string(), Box::new(resolver));
            registry
        };

        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(RunnerRegistry::new(runners), resolvers),
            namespace_capabilities: vec![],
        }));
        let realm = model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await?;
        assert_eq!(realm.component_url, "test:///b");

        let registered_runner = realm.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner.as_ref(), Some((None, r)) if r == &runner_reg);
        assert_matches!(realm.environment.get_registered_runner(&"foo".into()), Ok(None));

        Ok(())
    }

    // A component declares an environment that inherits from realm, and the realm's environment
    // added something that should be available in the component's realm.
    #[fasync::run_singlethreaded(test)]
    async fn test_inherit_parent() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: cm_rust::RegistrationSource::Parent,
            source_name: "test-src".into(),
        };
        let runners: HashMap<cm_rust::CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let mut resolver = MockResolver::new();
        resolver.add_component(
            "root",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("a").url("test:///a").environment("env_a"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_a")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(cm_rust::RunnerRegistration {
                            source: cm_rust::RegistrationSource::Parent,
                            source_name: "test-src".into(),
                            target_name: "test".into(),
                        }),
                )
                .build(),
        );
        resolver.add_component(
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("b").url("test:///b").environment("env_b"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_b")
                        .extends(fsys::EnvironmentExtends::Realm),
                )
                .build(),
        );
        resolver.add_component("b", ComponentDeclBuilder::new_empty_component().build());
        let resolvers = {
            let mut registry = ResolverRegistry::new();
            registry.register("test".to_string(), Box::new(resolver));
            registry
        };

        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(RunnerRegistry::new(runners), resolvers),
            namespace_capabilities: vec![],
        }));
        let realm = model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await?;
        assert_eq!(realm.component_url, "test:///b");

        let registered_runner = realm.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner.as_ref(), Some((Some(_), r)) if r == &runner_reg);
        let parent_moniker = &registered_runner.unwrap().0.unwrap().abs_moniker;
        assert_eq!(parent_moniker, &AbsoluteMoniker::root());
        assert_matches!(realm.environment.get_registered_runner(&"foo".into()), Ok(None));

        Ok(())
    }

    // A component in a collection declares an environment that inherits from realm, and the
    // realm's environment added something that should be available in the component's realm.
    #[fasync::run_singlethreaded(test)]
    async fn test_inherit_in_collection() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: cm_rust::RegistrationSource::Parent,
            source_name: "test-src".into(),
        };
        let runners: HashMap<cm_rust::CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let mut resolver = MockResolver::new();
        resolver.add_component(
            "root",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("a").url("test:///a").environment("env_a"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_a")
                        .extends(fsys::EnvironmentExtends::Realm)
                        .add_runner(cm_rust::RunnerRegistration {
                            source: cm_rust::RegistrationSource::Parent,
                            source_name: "test-src".into(),
                            target_name: "test".into(),
                        }),
                )
                .build(),
        );
        resolver.add_component(
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_collection(
                    CollectionDeclBuilder::new_transient_collection("coll").environment("env_b"),
                )
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_b")
                        .extends(fsys::EnvironmentExtends::Realm),
                )
                .build(),
        );
        resolver.add_component("b", ComponentDeclBuilder::new_empty_component().build());
        let resolvers = {
            let mut registry = ResolverRegistry::new();
            registry.register("test".to_string(), Box::new(resolver));
            registry
        };

        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(RunnerRegistry::new(runners), resolvers),
            namespace_capabilities: vec![],
        }));
        // Add instance to collection.
        {
            let parent_realm = model.bind(&vec!["a:0"].into(), &BindReason::Eager).await?;
            let child_decl = ChildDeclBuilder::new_lazy_child("b").build();
            parent_realm
                .add_dynamic_child("coll".into(), &child_decl)
                .await
                .expect("failed to add child");
        }
        let realm = model.bind(&vec!["a:0", "coll:b:1"].into(), &BindReason::Eager).await?;
        assert_eq!(realm.component_url, "test:///b");

        let registered_runner = realm.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner.as_ref(), Some((Some(_), r)) if r == &runner_reg);
        let parent_moniker = &registered_runner.unwrap().0.unwrap().abs_moniker;
        assert_eq!(parent_moniker, &AbsoluteMoniker::root());
        assert_matches!(realm.environment.get_registered_runner(&"foo".into()), Ok(None));

        Ok(())
    }

    // One of the components does not declare or specify an environment for the leaf child. The
    // leaf child component should still be able to access the resolvers of the root realm, as an
    // implicit inheriting environment is assumed.
    #[fasync::run_singlethreaded(test)]
    async fn test_auto_inheritance() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: cm_rust::RegistrationSource::Parent,
            source_name: "test-src".into(),
        };
        let runners: HashMap<cm_rust::CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let mut resolver = MockResolver::new();
        resolver.add_component(
            "root",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("a").url("test:///a").environment("env_a"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_a")
                        .extends(fsys::EnvironmentExtends::Realm),
                )
                .build(),
        );
        resolver.add_component(
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("b").url("test:///b"))
                .build(),
        );
        resolver.add_component("b", ComponentDeclBuilder::new_empty_component().build());
        let resolvers = {
            let mut registry = ResolverRegistry::new();
            registry.register("test".to_string(), Box::new(resolver));
            registry
        };

        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(RunnerRegistry::new(runners), resolvers),
            namespace_capabilities: vec![],
        }));

        let realm = model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await?;
        assert_eq!(realm.component_url, "test:///b");

        let registered_runner = realm.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner.as_ref(), Some((None, r)) if r == &runner_reg);
        assert_matches!(realm.environment.get_registered_runner(&"foo".into()), Ok(None));

        Ok(())
    }

    // One of the components declares an environment that does not inherit from the realm. This
    // means that any child components of this component cannot be resolved.
    #[fasync::run_singlethreaded(test)]
    async fn test_resolver_no_inheritance() -> Result<(), ModelError> {
        let mut resolver = MockResolver::new();
        resolver.add_component(
            "root",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("a").url("test:///a").environment("env_a"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_a")
                        .extends(fsys::EnvironmentExtends::Realm),
                )
                .build(),
        );
        resolver.add_component(
            "a",
            ComponentDeclBuilder::new_empty_component()
                .add_child(ChildDeclBuilder::new().name("b").url("test:///b").environment("env_b"))
                .add_environment(
                    EnvironmentDeclBuilder::new()
                        .name("env_b")
                        .extends(fsys::EnvironmentExtends::None)
                        .stop_timeout(1234),
                )
                .build(),
        );
        resolver.add_component("b", ComponentDeclBuilder::new_empty_component().build());
        let registry = {
            let mut registry = ResolverRegistry::new();
            registry.register("test".to_string(), Box::new(resolver));
            registry
        };
        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(RunnerRegistry::default(), registry),
            namespace_capabilities: vec![],
        }));
        assert_matches!(
            model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await,
            Err(ModelError::ResolverError { .. })
        );
        Ok(())
    }
}
