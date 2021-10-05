// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::component::ComponentInstance,
    crate::model::resolver::{ResolvedComponent, Resolver, ResolverError},
    ::routing::{environment::find_first_absolute_ancestor_url, error::ComponentInstanceError},
    anyhow::anyhow,
    async_trait::async_trait,
    clonable_error::ClonableError,
    std::sync::Arc,
    url::Url,
};

pub static SCHEME: &str = "";

fn validate_relative_url(url: &str) -> Result<(), ResolverError> {
    if url.len() == 0 {
        return Err(ResolverError::MalformedUrl(ClonableError::from(anyhow!(
            "Tried to validate an empty relative URL"
        ))));
    }

    if url.chars().nth(0) != Some('#') {
        return Err(ResolverError::MalformedUrl(ClonableError::from(anyhow!(
            "Relative URLs must be only resources, and start with '#'"
        ))));
    }
    let result = Url::parse(url);
    match result {
        Ok(_) => {
            return Err(ResolverError::MalformedUrl(ClonableError::from(anyhow!(
                "Tried to validate a relative url but we found a full URL"
            ))));
        }
        Err(e) => {
            if e != url::ParseError::RelativeUrlWithoutBase {
                return Err(ResolverError::MalformedUrl(ClonableError::from(anyhow!(
                    "Failed to parse relative url: {}",
                    e
                ))));
            }
        }
    }

    Ok(())
}

pub struct RelativeResolver;

impl RelativeResolver {
    pub fn new() -> RelativeResolver {
        RelativeResolver {}
    }

    async fn resolve_async<'a>(
        &'a self,
        component_url: &'a str,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        validate_relative_url(component_url)?;
        let absolute_url = find_first_absolute_ancestor_url(target).map_err(|e| match e {
            ComponentInstanceError::MalformedUrl { url, moniker } => {
                ResolverError::malformed_url(ComponentInstanceError::MalformedUrl {
                    url: url.clone(),
                    moniker: moniker.clone(),
                })
            }
            ComponentInstanceError::NoAbsoluteUrl { url, moniker } => {
                ResolverError::internal(ComponentInstanceError::NoAbsoluteUrl {
                    url: url.clone(),
                    moniker: moniker.clone(),
                })
            }
            _ => ResolverError::Internal(ClonableError::from(anyhow::Error::from(e))),
        })?;
        let rebased_url = absolute_url.join(component_url).unwrap();

        target.environment.resolve(rebased_url.as_str(), target).await
    }
}

#[async_trait]
impl Resolver for RelativeResolver {
    async fn resolve(
        &self,
        component_url: &str,
        target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        self.resolve_async(component_url, target).await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::builtin::relative_resolver::RelativeResolver,
        crate::model::{
            component::{
                ComponentInstance, ComponentManagerInstance, WeakComponentInstance,
                WeakExtendedInstance,
            },
            context::WeakModelContext,
            environment::{DebugRegistry, Environment, RunnerRegistry},
            hooks::Hooks,
            resolver::{Resolver, ResolverRegistry},
        },
        anyhow::Error,
        async_trait::async_trait,
        fidl_fuchsia_sys2 as fsys,
        matches::assert_matches,
        moniker::AbsoluteMonikerBase,
        std::sync::Arc,
        std::sync::Weak,
    };

    struct MockOkResolver {
        pub expected_url: String,
        pub resolved_url: String,
    }

    #[async_trait]
    impl Resolver for MockOkResolver {
        async fn resolve(
            &self,
            component_url: &str,
            _target: &Arc<ComponentInstance>,
        ) -> Result<ResolvedComponent, ResolverError> {
            assert_eq!(self.expected_url, component_url);
            Ok(ResolvedComponent {
                resolved_url: self.resolved_url.clone(),
                decl: fsys::ComponentDecl { ..fsys::ComponentDecl::EMPTY },
                package: None,
            })
        }
    }

    #[fuchsia::test]
    async fn test_validate_relative_url() -> Result<(), Error> {
        assert_matches!(
            validate_relative_url("fuchsia-pkg://fuchsia.com/full-url"),
            Err(ResolverError::MalformedUrl(..))
        );
        assert_matches!(validate_relative_url(""), Err(ResolverError::MalformedUrl(..)));
        assert_matches!(
            validate_relative_url("poorly-formed-fragment"),
            Err(ResolverError::MalformedUrl(..))
        );
        assert_matches!(
            validate_relative_url("path/and/then#fragment"),
            Err(ResolverError::MalformedUrl(..))
        );
        assert_matches!(validate_relative_url("#correct/fragment.cml"), Ok(()));
        Ok(())
    }

    #[fuchsia::test]
    async fn relative_to_fuchsia_pkg() -> Result<(), Error> {
        let expected_url: &str = "fuchsia-pkg://fuchsia.com/my-package#meta/my-child.cml";
        let mut resolver = ResolverRegistry::new();

        resolver.register(SCHEME.to_string(), Box::new(RelativeResolver::new()));

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockOkResolver {
                expected_url: expected_url.to_string(),
                resolved_url: expected_url.to_string(),
            }),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cml".to_string(),
        );

        let child = ComponentInstance::new(
            root.environment.clone(),
            moniker::AbsoluteMoniker::parse_string_without_instances("/root/child")?,
            "#meta/my-child.cml".to_string(),
            fsys::StartupMode::Lazy,
            fsys::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new(None)),
            None,
        );

        let resolved = child.environment.resolve(&child.component_url, &child).await?;
        assert_eq!(resolved.resolved_url, expected_url);
        Ok(())
    }

    #[fuchsia::test]
    async fn two_relative_to_fuchsia_pkg() -> Result<(), Error> {
        let expected_url: &str = "fuchsia-pkg://fuchsia.com/my-package#meta/my-child2.cml";
        let mut resolver = ResolverRegistry::new();

        resolver.register(SCHEME.to_string(), Box::new(RelativeResolver::new()));

        resolver.register(
            "fuchsia-pkg".to_string(),
            Box::new(MockOkResolver {
                expected_url: expected_url.to_string(),
                resolved_url: expected_url.to_string(),
            }),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-pkg://fuchsia.com/my-package#meta/my-root.cml".to_string(),
        );

        let child_one = ComponentInstance::new(
            root.environment.clone(),
            moniker::AbsoluteMoniker::parse_string_without_instances("/root/child")?,
            "#meta/my-child.cml".to_string(),
            fsys::StartupMode::Lazy,
            fsys::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new(None)),
            None,
        );

        let child_two = ComponentInstance::new(
            root.environment.clone(),
            moniker::AbsoluteMoniker::parse_string_without_instances("/root/child")?,
            "#meta/my-child2.cml".to_string(),
            fsys::StartupMode::Lazy,
            fsys::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&child_one)),
            Arc::new(Hooks::new(None)),
            None,
        );

        let resolved = child_two.environment.resolve(&child_two.component_url, &child_two).await?;
        assert_eq!(resolved.resolved_url, expected_url);
        Ok(())
    }

    #[fuchsia::test]
    async fn relative_to_fuchsia_boot() -> Result<(), Error> {
        let expected_url: &str = "fuchsia-boot:///#meta/my-child.cml";
        let mut resolver = ResolverRegistry::new();

        resolver.register(SCHEME.to_string(), Box::new(RelativeResolver::new()));

        resolver.register(
            "fuchsia-boot".to_string(),
            Box::new(MockOkResolver {
                expected_url: expected_url.to_string(),
                resolved_url: expected_url.to_string(),
            }),
        );

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "fuchsia-boot:///#meta/my-root.cml".to_string(),
        );

        let child = ComponentInstance::new(
            root.environment.clone(),
            moniker::AbsoluteMoniker::parse_string_without_instances("/root/child")?,
            "#meta/my-child.cml".to_string(),
            fsys::StartupMode::Lazy,
            fsys::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new(None)),
            None,
        );

        let resolved = child.environment.resolve(&child.component_url, &child).await?;
        assert_eq!(resolved.resolved_url, expected_url);
        Ok(())
    }

    #[fuchsia::test]
    async fn resolve_above_root_error() -> Result<(), Error> {
        let mut resolver = ResolverRegistry::new();

        resolver.register(SCHEME.to_string(), Box::new(RelativeResolver::new()));

        let top_instance = Arc::new(ComponentManagerInstance::new(vec![], vec![]));
        let environment = Environment::new_root(
            &top_instance,
            RunnerRegistry::default(),
            resolver,
            DebugRegistry::default(),
        );
        let root = ComponentInstance::new_root(
            environment,
            Weak::new(),
            Weak::new(),
            "#meta/my-root.cml".to_string(),
        );

        let child = ComponentInstance::new(
            root.environment.clone(),
            moniker::AbsoluteMoniker::parse_string_without_instances("/root/child")?,
            "#meta/my-child.cml".to_string(),
            fsys::StartupMode::Lazy,
            fsys::OnTerminate::None,
            WeakModelContext::new(Weak::new()),
            WeakExtendedInstance::Component(WeakComponentInstance::from(&root)),
            Arc::new(Hooks::new(None)),
            None,
        );

        let result = child.environment.resolve(&child.component_url, &child).await;
        assert_matches!(result, Err(ResolverError::Internal(..)));
        Ok(())
    }
}
