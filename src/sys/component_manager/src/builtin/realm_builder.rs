// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module is not for production instances of component_manager. It exists to allow a test
//! driver to define a custom realm with realm builder and to then launch a nested component
//! manager which will run that custom realm, for the sole purposes of integration testing
//! component manager behavior.

use {
    crate::{
        builtin::{capability::BuiltinCapability, runner::BuiltinRunnerFactory},
        model::component::ComponentInstance,
        model::resolver::{self, Resolver},
    },
    ::routing::resolving::{ComponentAddress, ResolvedComponent, ResolverError},
    ::routing::{capability_source::InternalCapability, policy::ScopedPolicyChecker},
    anyhow::Error,
    async_trait::async_trait,
    cm_runner::Runner,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_resolution as fresolution, fidl_fuchsia_component_runner as fcrunner,
    fuchsia_component::client as fclient,
    futures::TryStreamExt,
    std::convert::TryInto,
    std::sync::Arc,
};

pub static SCHEME: &str = "realm-builder";
pub static RUNNER_NAME: &str = "realm_builder";

/// Resolves component URLs with the "realm-builder" scheme, which supports loading components from
/// the fuchsia.component.resolution.Resolver protocol in component_manager's namespace.
///
/// Also runs components with the "realm-builder" runner, which supports launching components
/// through the fuchsia.component.runner.ComponentRunner protocol in component manager's namespace.
///
/// Both of these protocols are typically implemented by the realm builder library, for use when
/// integration testing a nested component manager.
#[derive(Debug)]
pub struct RealmBuilderResolver {
    resolver_proxy: fresolution::ResolverProxy,
}

impl RealmBuilderResolver {
    /// Create a new RealmBuilderResolver. This opens connections to the needed protocols
    /// in the namespace.
    pub fn new() -> Result<RealmBuilderResolver, Error> {
        Ok(RealmBuilderResolver {
            resolver_proxy: fclient::connect_to_protocol_at_path::<fresolution::ResolverMarker>(
                "/svc/fuchsia.component.resolver.RealmBuilder",
            )?,
        })
    }

    async fn resolve_async(
        &self,
        component_url: &str,
        some_incoming_context: Option<&fresolution::Context>,
    ) -> Result<fresolution::Component, fresolution::ResolverError> {
        let res = if let Some(context) = some_incoming_context {
            self.resolver_proxy
                .resolve_with_context(component_url, &mut context.clone())
                .await
                .expect("resolve_with_context failed in realm builder resolver")
        } else {
            self.resolver_proxy
                .resolve(component_url)
                .await
                .expect("resolve failed in realm builder resolver")
        };
        res
    }
}

#[async_trait]
impl Resolver for RealmBuilderResolver {
    async fn resolve(
        &self,
        component_address: &ComponentAddress,
        _target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        let (component_url, some_context) = component_address.to_url_and_context();
        let fresolution::Component {
            url, decl, package, config_values, resolution_context, ..
        } = self
            .resolve_async(component_url, some_context.map(|context| context.into()).as_ref())
            .await?;
        let resolved_by = "RealmBuilderResolver".to_string();
        let resolved_url = url.unwrap();
        let context_to_resolve_children = resolution_context.map(Into::into);
        let decl = resolver::read_and_validate_manifest(&decl.unwrap()).await?;
        let config_values = if let Some(data) = config_values {
            Some(resolver::read_and_validate_config_values(&data)?)
        } else {
            None
        };
        Ok(ResolvedComponent {
            resolved_by,
            resolved_url,
            context_to_resolve_children,
            decl,
            package: package.map(|p| p.try_into()).transpose()?,
            config_values,
        })
    }
}

#[async_trait]
impl BuiltinCapability for RealmBuilderResolver {
    const NAME: &'static str = "realm_builder_resolver";
    type Marker = fresolution::ResolverMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fresolution::ResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                fresolution::ResolverRequest::Resolve { component_url, responder } => {
                    responder.send(&mut self.resolve_async(&component_url, None).await)?;
                }
                fresolution::ResolverRequest::ResolveWithContext {
                    component_url,
                    context,
                    responder,
                } => {
                    responder
                        .send(&mut self.resolve_async(&component_url, Some(&context)).await)?;
                }
            }
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        let res = match capability {
            InternalCapability::Resolver(name) if *name == Self::NAME => true,
            _ => false,
        };
        res
    }
}

pub struct RealmBuilderRunner {
    runner_proxy: fcrunner::ComponentRunnerProxy,
}

impl RealmBuilderRunner {
    /// Create a new RealmBuilderRunner. This opens connections to the needed protocols
    /// in the namespace.
    pub fn new() -> Result<RealmBuilderRunner, Error> {
        Ok(RealmBuilderRunner {
            runner_proxy: fclient::connect_to_protocol_at_path::<fcrunner::ComponentRunnerMarker>(
                "/svc/fuchsia.component.runner.RealmBuilder",
            )?,
        })
    }
}

impl BuiltinRunnerFactory for RealmBuilderRunner {
    fn get_scoped_runner(self: Arc<Self>, _checker: ScopedPolicyChecker) -> Arc<dyn Runner> {
        self.clone()
    }
}

#[async_trait]
impl Runner for RealmBuilderRunner {
    async fn start(
        &self,
        start_info: fcrunner::ComponentStartInfo,
        server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) {
        self.runner_proxy
            .start(start_info, server_end)
            .expect("failed to use realm builder runner");
    }
}
