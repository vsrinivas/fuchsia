// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        realm::Realm,
        resolver::{Resolver, ResolverError, ResolverFut, ResolverRegistry},
    },
    cm_rust::EnvironmentDecl,
    fidl_fuchsia_sys2 as fsys,
    std::sync::{Arc, Weak},
};

/// A realm's environment, populated from a component's [`EnvironmentDecl`].
/// An environment defines intrinsic behaviors of a component's realm. Components
/// can define an environment, but do not interact with it directly.
///
/// [`EnvironmentDecl`]: fidl_fuchsia_sys2::EnvironmentDecl
pub struct Environment {
    parent: Weak<Realm>,
    resolver_registry: ResolverRegistry,
}

impl Environment {
    /// Creates a new empty environment without a parent.
    pub fn empty() -> Environment {
        Environment { parent: Weak::new(), resolver_registry: ResolverRegistry::new() }
    }

    /// Creates a new root environment with a resolver registry and no parent.
    pub fn new_root(resolver_registry: ResolverRegistry) -> Environment {
        Environment { parent: Weak::new(), resolver_registry }
    }

    /// Creates an environment from `env_decl`, using `realm` as the parent if necessary.
    pub fn from_decl(realm: &Arc<Realm>, env_decl: &EnvironmentDecl) -> Environment {
        Environment {
            parent: match env_decl.extends {
                fsys::EnvironmentExtends::Realm => Arc::downgrade(realm),
                fsys::EnvironmentExtends::None => Weak::new(),
            },
            resolver_registry: ResolverRegistry::new(),
        }
    }

    /// Creates a new environment with `realm` as the parent.
    pub fn new_inheriting(realm: &Arc<Realm>) -> Environment {
        Environment { parent: Arc::downgrade(realm), resolver_registry: ResolverRegistry::new() }
    }
}

impl Resolver for Environment {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut<'a> {
        Box::pin(async move {
            match self.resolver_registry.resolve(component_url).await {
                Err(ResolverError::SchemeNotRegistered) => {
                    if let Some(parent_realm) = self.parent.upgrade() {
                        parent_realm.environment.resolve(component_url).await
                    } else {
                        Err(ResolverError::SchemeNotRegistered)
                    }
                }
                result => result,
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            binding::Binder,
            error::ModelError,
            model::{Model, ModelParams},
            testing::{
                mocks::MockResolver,
                test_helpers::{ChildDeclBuilder, ComponentDeclBuilder, EnvironmentDeclBuilder},
            },
        },
        fuchsia_async as fasync,
        matches::assert_matches,
        std::collections::HashMap,
    };

    #[test]
    fn test_from_decl() {
        let realm =
            Arc::new(Realm::new_root_realm(Environment::empty(), "test:///root".to_string()));
        let environment = Environment::from_decl(
            &realm,
            &EnvironmentDeclBuilder::new()
                .name("env")
                .extends(fsys::EnvironmentExtends::None)
                .build(),
        );
        assert_matches!(environment.parent.upgrade(), None);

        let environment = Environment::from_decl(
            &realm,
            &EnvironmentDeclBuilder::new()
                .name("env")
                .extends(fsys::EnvironmentExtends::Realm)
                .build(),
        );
        assert_matches!(environment.parent.upgrade(), Some(_));
    }

    // Each component declares an environment for their child that inherits from
    // the realm's environment. The leaf component should be able to access the
    // resolvers of the root realm.
    #[fasync::run_singlethreaded(test)]
    async fn test_resolver_inheritance() -> Result<(), ModelError> {
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
        let registry = {
            let mut registry = ResolverRegistry::new();
            registry.register("test".to_string(), Box::new(resolver));
            registry
        };
        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_resolver_registry: registry,
            builtin_runners: HashMap::new(),
        }));
        let realm = model.bind(&vec!["a:0", "b:0"].into()).await?;
        assert_eq!(realm.component_url, "test:///b");
        Ok(())
    }

    // One of the components does not declare or specify an environment for the
    // leaf child. The leaf child component should still be able to access the
    // resolvers of the root realm, as an implicit inheriting environment is
    // assumed.
    #[fasync::run_singlethreaded(test)]
    async fn test_resolver_auto_inheritance() -> Result<(), ModelError> {
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
        let registry = {
            let mut registry = ResolverRegistry::new();
            registry.register("test".to_string(), Box::new(resolver));
            registry
        };
        let model = Arc::new(Model::new(ModelParams {
            root_component_url: "test:///root".to_string(),
            root_resolver_registry: registry,
            builtin_runners: HashMap::new(),
        }));
        let realm = model.bind(&vec!["a:0", "b:0"].into()).await?;
        assert_eq!(realm.component_url, "test:///b");
        Ok(())
    }

    // One of the components declares an environment that does not inherit from
    // the realm. This means that any child components of this component cannot
    // be resolved.
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
                        .extends(fsys::EnvironmentExtends::None),
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
            root_resolver_registry: registry,
            builtin_runners: HashMap::new(),
        }));
        assert_matches!(
            model.bind(&vec!["a:0", "b:0"].into()).await,
            Err(ModelError::ResolverError { .. })
        );
        Ok(())
    }
}
