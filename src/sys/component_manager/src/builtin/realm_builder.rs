// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module is not for production instances of component_manager. It exists to allow a test
//! driver to define a custom realm with realm builder and to then launch a nested component
//! manager which will run that custom realm, for the sole purposes of integration testing
//! component manager behavior.

use {
    crate::model::component::ComponentInstance,
    crate::{
        builtin::{capability::BuiltinCapability, runner::BuiltinRunnerFactory},
        capability::InternalCapability,
        model::{
            policy::ScopedPolicyChecker,
            resolver::{self, ResolvedComponent, Resolver, ResolverError},
            runner::Runner,
        },
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_sys2 as fsys,
    fuchsia_component::client as fclient,
    futures::TryStreamExt,
    std::sync::Arc,
};

pub static SCHEME: &str = "realm-builder";
pub static RUNNER_NAME: &str = "realm_builder";

/// Resolves component URLs with the "realm-builder" scheme, which supports loading components from
/// the fuchsia.sys2.ComponentResolver protocol in component_manager's namespace.
///
/// Also runs components with the "realm-builder" runner, which supports launching components
/// through the fuchsia.component.runner.ComponentRunner protocol in component manager's namespace.
///
/// Both of these protocols are typically implemented by the realm builder library, for use when
/// integration testing a nested component manager.
pub struct RealmBuilderResolver {
    resolver_proxy: fsys::ComponentResolverProxy,
}

impl RealmBuilderResolver {
    /// Create a new RealmBuilderResolver. This opens connections to the needed protocols
    /// in the namespace.
    pub fn new() -> Result<RealmBuilderResolver, Error> {
        Ok(RealmBuilderResolver {
            resolver_proxy: fclient::connect_to_protocol_at_path::<fsys::ComponentResolverMarker>(
                "/svc/fuchsia.component.resolver.RealmBuilder",
            )?,
        })
    }

    async fn resolve_async(
        &self,
        component_url: &str,
    ) -> Result<fsys::Component, fsys::ResolverError> {
        let res = self
            .resolver_proxy
            .resolve(component_url)
            .await
            .expect("failed to use realm builder resolver");
        res
    }
}

#[async_trait]
impl Resolver for RealmBuilderResolver {
    async fn resolve(
        &self,
        component_url: &str,
        _target: &Arc<ComponentInstance>,
    ) -> Result<ResolvedComponent, ResolverError> {
        let fsys::Component { resolved_url, decl, package, .. } =
            self.resolve_async(component_url).await?;
        let resolved_url = resolved_url.unwrap();
        let decl = resolver::read_and_validate_manifest(decl.unwrap()).await?;
        Ok(ResolvedComponent { resolved_url, decl, package })
    }
}

#[async_trait]
impl BuiltinCapability for RealmBuilderResolver {
    const NAME: &'static str = "realm_builder_resolver";
    type Marker = fsys::ComponentResolverMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: fsys::ComponentResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(fsys::ComponentResolverRequest::Resolve { component_url, responder }) =
            stream.try_next().await?
        {
            responder.send(&mut self.resolve_async(&component_url).await)?;
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
