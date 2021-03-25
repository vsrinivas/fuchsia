// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{
            ComponentInstance, ComponentManagerInstance, ExtendedInstance, WeakExtendedInstance,
        },
        error::ModelError,
        resolver::{ResolvedComponent, Resolver, ResolverError, ResolverRegistry},
    },
    async_trait::async_trait,
    cm_rust::{CapabilityName, EnvironmentDecl, RegistrationSource, RunnerRegistration},
    fidl_fuchsia_sys2 as fsys,
    std::{
        collections::HashMap,
        sync::{Arc, Weak},
        time::Duration,
    },
};

/// A realm's environment, populated from a component's [`EnvironmentDecl`].
/// An environment defines intrinsic behaviors of a component's realm. Components
/// can define an environment, but do not interact with it directly.
///
/// [`EnvironmentDecl`]: fidl_fuchsia_sys2::EnvironmentDecl
pub struct Environment {
    /// Name of this environment as defined by its creator.
    /// Would be `None` for root environment.
    name: Option<String>,
    /// The parent that created or inherited the environment.
    parent: WeakExtendedInstance,
    /// The extension mode of this environment.
    extends: EnvironmentExtends,
    /// The runners available in this environment.
    runner_registry: RunnerRegistry,
    /// The resolvers in this environment, mapped to URL schemes.
    resolver_registry: ResolverRegistry,
    /// Protocols available in this environment as debug capabilities.
    debug_registry: DebugRegistry,
    /// The deadline for runners to respond to `ComponentController.Stop` calls.
    stop_timeout: Duration,
}

pub const DEFAULT_STOP_TIMEOUT: Duration = Duration::from_secs(5);

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
    /// Creates a new empty environment parented to component manager.
    pub fn empty() -> Environment {
        Environment {
            name: None,
            parent: WeakExtendedInstance::AboveRoot(Weak::new()),
            extends: EnvironmentExtends::None,
            runner_registry: RunnerRegistry::default(),
            resolver_registry: ResolverRegistry::new(),
            debug_registry: DebugRegistry::default(),
            stop_timeout: DEFAULT_STOP_TIMEOUT,
        }
    }

    /// Creates a new root environment with a resolver registry, parented to component manager.
    pub fn new_root(
        top_instance: &Arc<ComponentManagerInstance>,
        runner_registry: RunnerRegistry,
        resolver_registry: ResolverRegistry,
        debug_registry: DebugRegistry,
    ) -> Environment {
        Environment {
            name: None,
            parent: WeakExtendedInstance::AboveRoot(Arc::downgrade(top_instance)),
            extends: EnvironmentExtends::None,
            runner_registry,
            resolver_registry,
            debug_registry,
            stop_timeout: DEFAULT_STOP_TIMEOUT,
        }
    }

    /// Creates an environment from `env_decl`, using `parent` as the parent realm.
    pub fn from_decl(parent: &Arc<ComponentInstance>, env_decl: &EnvironmentDecl) -> Environment {
        Environment {
            name: Some(env_decl.name.clone()),
            parent: WeakExtendedInstance::Component(parent.into()),
            extends: env_decl.extends.into(),
            runner_registry: RunnerRegistry::from_decl(&env_decl.runners),
            resolver_registry: ResolverRegistry::from_decl(&env_decl.resolvers, parent),
            debug_registry: env_decl.debug_capabilities.clone().into(),
            stop_timeout: match env_decl.stop_timeout_ms {
                Some(timeout) => Duration::from_millis(timeout.into()),
                None => match env_decl.extends {
                    fsys::EnvironmentExtends::Realm => parent.environment.stop_timeout(),
                    fsys::EnvironmentExtends::None => {
                        panic!("EnvironmentDecl is missing stop_timeout");
                    }
                },
            },
        }
    }

    /// Creates a new environment with `parent` as the parent.
    pub fn new_inheriting(parent: &Arc<ComponentInstance>) -> Environment {
        Environment {
            name: None,
            parent: WeakExtendedInstance::Component(parent.into()),
            extends: EnvironmentExtends::Realm,
            runner_registry: RunnerRegistry::default(),
            resolver_registry: ResolverRegistry::new(),
            debug_registry: DebugRegistry::default(),
            stop_timeout: parent.environment.stop_timeout(),
        }
    }

    pub fn stop_timeout(&self) -> Duration {
        self.stop_timeout
    }

    /// Returns the runner registered to `name` and the component that created the environment the
    /// runner was registered to. Returns `None` if there was no match.
    pub fn get_registered_runner(
        &self,
        name: &CapabilityName,
    ) -> Result<Option<(ExtendedInstance, RunnerRegistration)>, ModelError> {
        let parent = self.parent.upgrade()?;
        match self.runner_registry.get_runner(name) {
            Some(reg) => Ok(Some((parent, reg.clone()))),
            None => match self.extends {
                EnvironmentExtends::Realm => match parent {
                    ExtendedInstance::Component(parent) => {
                        parent.environment.get_registered_runner(name)
                    }
                    ExtendedInstance::AboveRoot(_) => {
                        unreachable!("root env can't extend")
                    }
                },
                EnvironmentExtends::None => {
                    return Ok(None);
                }
            },
        }
    }

    /// Returns the debug capability registered to `name`, the realm that created the environment
    /// and the capability was registered to (`None` for component manager's realm) and name of the
    /// environment that registered the capability. Returns `None` if there was no match.
    pub fn get_debug_capability(
        &self,
        name: &CapabilityName,
    ) -> Result<Option<(ExtendedInstance, Option<String>, DebugRegistration)>, ModelError> {
        let parent = self.parent.upgrade()?;
        match self.debug_registry.get_capability(name) {
            Some(reg) => Ok(Some((parent, self.name.clone(), reg.clone()))),
            None => match self.extends {
                EnvironmentExtends::Realm => match parent {
                    ExtendedInstance::Component(parent) => {
                        parent.environment.get_debug_capability(name)
                    }
                    ExtendedInstance::AboveRoot(_) => {
                        unreachable!("root env can't extend")
                    }
                },
                EnvironmentExtends::None => {
                    return Ok(None);
                }
            },
        }
    }

    pub fn name(&self) -> &Option<String> {
        &self.name
    }

    pub fn parent(&self) -> &WeakExtendedInstance {
        &self.parent
    }
}

#[async_trait]
impl Resolver for Environment {
    async fn resolve(&self, component_url: &str) -> Result<ResolvedComponent, ResolverError> {
        let parent = self.parent.upgrade().map_err(|_| ResolverError::SchemeNotRegistered)?;
        match self.resolver_registry.resolve(component_url).await {
            Err(ResolverError::SchemeNotRegistered) => match &self.extends {
                EnvironmentExtends::Realm => match parent {
                    ExtendedInstance::Component(parent) => {
                        parent.environment.resolve(component_url).await
                    }
                    ExtendedInstance::AboveRoot(_) => {
                        unreachable!("root env can't extend")
                    }
                },
                EnvironmentExtends::None => Err(ResolverError::SchemeNotRegistered),
            },
            result => result,
        }
    }
}

/// The set of runners available in a realm's environment.
///
/// [`RunnerRegistration`]: fidl_fuchsia_sys2::RunnerRegistration
pub struct RunnerRegistry {
    runners: HashMap<CapabilityName, RunnerRegistration>,
}

impl RunnerRegistry {
    pub fn default() -> Self {
        Self { runners: HashMap::new() }
    }

    pub fn new(runners: HashMap<CapabilityName, RunnerRegistration>) -> Self {
        Self { runners }
    }

    pub fn from_decl(regs: &Vec<RunnerRegistration>) -> Self {
        let mut runners = HashMap::new();
        for reg in regs {
            runners.insert(reg.target_name.clone(), reg.clone());
        }
        Self { runners }
    }
    pub fn get_runner(&self, name: &CapabilityName) -> Option<&RunnerRegistration> {
        self.runners.get(name)
    }
}

/// The set of debug capabilities available in this environment.
#[derive(Default, Debug, Clone, PartialEq, Eq)]
pub struct DebugRegistry {
    debug_capabilities: HashMap<CapabilityName, DebugRegistration>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DebugRegistration {
    pub source: RegistrationSource,
    pub source_name: CapabilityName,
}

impl From<Vec<cm_rust::DebugRegistration>> for DebugRegistry {
    fn from(regs: Vec<cm_rust::DebugRegistration>) -> Self {
        let mut debug_capabilities = HashMap::new();
        for reg in regs {
            match reg {
                cm_rust::DebugRegistration::Protocol(r) => {
                    debug_capabilities.insert(
                        r.target_name,
                        DebugRegistration { source_name: r.source_name, source: r.source },
                    );
                }
            };
        }
        Self { debug_capabilities }
    }
}

impl DebugRegistry {
    pub fn get_capability(&self, name: &CapabilityName) -> Option<&DebugRegistration> {
        self.debug_capabilities.get(name)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::config::RuntimeConfig,
        crate::model::{
            binding::Binder,
            component::BindReason,
            error::ModelError,
            model::{Model, ModelParams},
            testing::{
                mocks::MockResolver,
                test_helpers::{
                    ChildDeclBuilder, CollectionDeclBuilder, ComponentDeclBuilder,
                    EnvironmentDeclBuilder,
                },
            },
        },
        maplit::hashmap,
        matches::assert_matches,
        moniker::AbsoluteMoniker,
        std::sync::Weak,
    };

    #[test]
    fn test_from_decl() {
        let component = ComponentInstance::new_root(
            Environment::empty(),
            Weak::new(),
            Weak::new(),
            "test:///root".to_string(),
        );
        let environment = Environment::from_decl(
            &component,
            &EnvironmentDeclBuilder::new()
                .name("env")
                .extends(fsys::EnvironmentExtends::None)
                .stop_timeout(1234)
                .build(),
        );
        assert_matches!(environment.parent, WeakExtendedInstance::Component(_));

        let environment = Environment::from_decl(
            &component,
            &EnvironmentDeclBuilder::new()
                .name("env")
                .extends(fsys::EnvironmentExtends::Realm)
                .build(),
        );
        assert_matches!(environment.parent, WeakExtendedInstance::Component(_));

        let environment = Environment::from_decl(
            &component,
            &EnvironmentDeclBuilder::new()
                .name("env")
                .extends(fsys::EnvironmentExtends::None)
                .stop_timeout(1234)
                .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                    cm_rust::DebugProtocolRegistration {
                        source_name: "source_name".into(),
                        target_name: "target_name".into(),
                        source: RegistrationSource::Parent,
                    },
                ))
                .build(),
        );
        let expected_debug_capability: HashMap<CapabilityName, DebugRegistration> = hashmap! {
            "target_name".into() =>
            DebugRegistration {
                source_name: "source_name".into(),
                source: RegistrationSource::Parent,
            }
        };
        assert_eq!(environment.debug_registry.debug_capabilities, expected_debug_capability);
    }

    // Each component declares an environment for their child that inherits from the component's
    // environment. The leaf component should be able to access the resolvers of the root.
    #[fuchsia::test]
    async fn test_inherit_root() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: RegistrationSource::Parent,
            source_name: "test-src".into(),
            target_name: "test".into(),
        };
        let runners: HashMap<cm_rust::CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let debug_reg = DebugRegistration {
            source_name: "source_name".into(),
            source: RegistrationSource::Self_,
        };

        let debug_capabilities: HashMap<cm_rust::CapabilityName, DebugRegistration> = hashmap! {
            "target_name".into() => debug_reg.clone()
        };
        let debug_registry = DebugRegistry { debug_capabilities };

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

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![]));
        let model = Model::new(ModelParams {
            runtime_config: Arc::new(RuntimeConfig::default()),
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(
                &top_instance,
                RunnerRegistry::new(runners),
                resolvers,
                debug_registry,
            ),
            top_instance,
        })
        .await
        .unwrap();
        let component = model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await?;
        assert_eq!(component.component_url, "test:///b");

        let registered_runner =
            component.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner, Some((ExtendedInstance::AboveRoot(_), r)) if r == runner_reg);
        assert_matches!(component.environment.get_registered_runner(&"foo".into()), Ok(None));

        let debug_capability =
            component.environment.get_debug_capability(&"target_name".into()).unwrap();
        assert_matches!(debug_capability, Some((ExtendedInstance::AboveRoot(_), None, d)) if d == debug_reg);
        assert_matches!(component.environment.get_debug_capability(&"foo".into()), Ok(None));

        Ok(())
    }

    // A component declares an environment that inherits from realm, and the realm's environment
    // added something that should be available in the component's realm.
    #[fuchsia::test]
    async fn test_inherit_parent() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: RegistrationSource::Parent,
            source_name: "test-src".into(),
            target_name: "test".into(),
        };
        let runners: HashMap<CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let debug_reg = DebugRegistration {
            source_name: "source_name".into(),
            source: RegistrationSource::Parent,
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
                        .add_runner(RunnerRegistration {
                            source: RegistrationSource::Parent,
                            source_name: "test-src".into(),
                            target_name: "test".into(),
                        })
                        .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                            cm_rust::DebugProtocolRegistration {
                                source_name: "source_name".into(),
                                target_name: "target_name".into(),
                                source: RegistrationSource::Parent,
                            },
                        )),
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

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![]));
        let model = Model::new(ModelParams {
            runtime_config: Arc::new(RuntimeConfig::default()),
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(
                &top_instance,
                RunnerRegistry::new(runners),
                resolvers,
                DebugRegistry::default(),
            ),
            top_instance,
        })
        .await?;
        let component = model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await?;
        assert_eq!(component.component_url, "test:///b");

        let registered_runner =
            component.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner, Some((ExtendedInstance::Component(c), r))
            if r == runner_reg && c.abs_moniker == AbsoluteMoniker::root());
        assert_matches!(component.environment.get_registered_runner(&"foo".into()), Ok(None));

        let debug_capability =
            component.environment.get_debug_capability(&"target_name".into()).unwrap();
        assert_matches!(debug_capability, Some((ExtendedInstance::Component(c), Some(_), d))
            if d == debug_reg && c.abs_moniker == AbsoluteMoniker::root());
        assert_matches!(component.environment.get_debug_capability(&"foo".into()), Ok(None));

        Ok(())
    }

    // A component in a collection declares an environment that inherits from realm, and the
    // realm's environment added something that should be available in the component's realm.
    #[fuchsia::test]
    async fn test_inherit_in_collection() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: RegistrationSource::Parent,
            source_name: "test-src".into(),
            target_name: "test".into(),
        };
        let runners: HashMap<CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let debug_reg = DebugRegistration {
            source_name: "source_name".into(),
            source: RegistrationSource::Parent,
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
                        .add_runner(RunnerRegistration {
                            source: RegistrationSource::Parent,
                            source_name: "test-src".into(),
                            target_name: "test".into(),
                        })
                        .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                            cm_rust::DebugProtocolRegistration {
                                source_name: "source_name".into(),
                                target_name: "target_name".into(),
                                source: RegistrationSource::Parent,
                            },
                        )),
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

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![]));
        let model = Model::new(ModelParams {
            runtime_config: Arc::new(RuntimeConfig::default()),
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(
                &top_instance,
                RunnerRegistry::new(runners),
                resolvers,
                DebugRegistry::default(),
            ),
            top_instance,
        })
        .await?;
        // Add instance to collection.
        {
            let parent = model.bind(&vec!["a:0"].into(), &BindReason::Eager).await?;
            let child_decl = ChildDeclBuilder::new_lazy_child("b").build();
            parent
                .add_dynamic_child("coll".into(), &child_decl)
                .await
                .expect("failed to add child");
        }
        let component = model.bind(&vec!["a:0", "coll:b:1"].into(), &BindReason::Eager).await?;
        assert_eq!(component.component_url, "test:///b");

        let registered_runner =
            component.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner, Some((ExtendedInstance::Component(c), r))
            if r == runner_reg && c.abs_moniker == AbsoluteMoniker::root());
        assert_matches!(component.environment.get_registered_runner(&"foo".into()), Ok(None));

        let debug_capability =
            component.environment.get_debug_capability(&"target_name".into()).unwrap();
        assert_matches!(debug_capability, Some((ExtendedInstance::Component(c), Some(n), d))
            if d == debug_reg && n == "env_a" && c.abs_moniker == AbsoluteMoniker::root());
        assert_matches!(component.environment.get_debug_capability(&"foo".into()), Ok(None));

        Ok(())
    }

    // One of the components does not declare or specify an environment for the leaf child. The
    // leaf child component should still be able to access the resolvers of the root, as an
    // implicit inheriting environment is assumed.
    #[fuchsia::test]
    async fn test_auto_inheritance() -> Result<(), ModelError> {
        let runner_reg = RunnerRegistration {
            source: RegistrationSource::Parent,
            source_name: "test-src".into(),
            target_name: "test".into(),
        };
        let runners: HashMap<CapabilityName, RunnerRegistration> = hashmap! {
            "test".into() => runner_reg.clone()
        };

        let debug_reg = DebugRegistration {
            source_name: "source_name".into(),
            source: RegistrationSource::Parent,
        };

        let debug_capabilities: HashMap<CapabilityName, DebugRegistration> = hashmap! {
            "target_name".into() => debug_reg.clone()
        };
        let debug_registry = DebugRegistry { debug_capabilities };

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

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![]));
        let model = Model::new(ModelParams {
            runtime_config: Arc::new(RuntimeConfig::default()),
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(
                &top_instance,
                RunnerRegistry::new(runners),
                resolvers,
                debug_registry,
            ),
            top_instance,
        })
        .await
        .unwrap();

        let component = model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await?;
        assert_eq!(component.component_url, "test:///b");

        let registered_runner =
            component.environment.get_registered_runner(&"test".into()).unwrap();
        assert_matches!(registered_runner, Some((ExtendedInstance::AboveRoot(_), r)) if r == runner_reg);
        assert_matches!(component.environment.get_registered_runner(&"foo".into()), Ok(None));

        let debug_capability =
            component.environment.get_debug_capability(&"target_name".into()).unwrap();
        assert_matches!(debug_capability, Some((ExtendedInstance::AboveRoot(_), None, d)) if d == debug_reg);
        assert_matches!(component.environment.get_debug_capability(&"foo".into()), Ok(None));

        Ok(())
    }

    // One of the components declares an environment that does not inherit from the realm. This
    // means that any child components of this component cannot be resolved.
    #[fuchsia::test]
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
        let top_instance = Arc::new(ComponentManagerInstance::new(vec![]));
        let model = Model::new(ModelParams {
            runtime_config: Arc::new(RuntimeConfig::default()),
            root_component_url: "test:///root".to_string(),
            root_environment: Environment::new_root(
                &top_instance,
                RunnerRegistry::default(),
                registry,
                DebugRegistry::default(),
            ),
            top_instance,
        })
        .await?;
        assert_matches!(
            model.bind(&vec!["a:0", "b:0"].into(), &BindReason::Eager).await,
            Err(ModelError::ResolverError { .. })
        );
        Ok(())
    }
}
