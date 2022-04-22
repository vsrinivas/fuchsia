// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::verify::collection::V2ComponentModel,
    anyhow::{anyhow, Context, Result},
    cm_fidl_analyzer::{
        component_instance::ComponentInstanceForAnalyzer,
        component_model::ComponentModelForAnalyzer, BreadthFirstModelWalker,
        ComponentInstanceVisitor, ComponentModelWalker,
    },
    cm_rust::{CapabilityName, UseDecl},
    moniker::AbsoluteMonikerBase,
    routing::{
        component_instance::{ComponentInstanceInterface, ExtendedInstanceInterface},
        RouteSource,
    },
    scrutiny::{model::controller::DataController, model::model::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::{collections::HashSet, path::PathBuf, sync::Arc},
};

/// ComponentResolversController
///
/// A DataController which returns a list component node paths of all
/// components that, in their environment, contain a resolver with the
///  given moniker for a scheme with access to a protocol.
#[derive(Default)]
pub struct ComponentResolversController {}

/// The expected query format.
#[derive(Debug, Deserialize, Serialize)]
pub struct ComponentResolverRequest {
    /// `resolver` URI scheme of interest
    pub scheme: String,
    /// Node path of the `resolver`
    pub moniker: String,
    /// Filter the results to components resolved with a `resolver` with access to a protocol
    pub protocol: String,
}

/// The response schema.
#[derive(Debug, Deserialize, Serialize)]
pub struct ComponentResolverResponse {
    /// Files accessed to perform this query, for depfile generation.
    pub deps: HashSet<PathBuf>,
    /// Component monikers that matched the query.
    pub monikers: Vec<String>,
}

/// Walks the tree for component node paths of all components that, in their
/// environment, contain a resolver with the given moniker for a scheme
/// with access to a protocol.
/// `monikers` contains the components which match the `request` parameters.
struct ComponentResolversVisitor {
    request: ComponentResolverRequest,
    monikers: Vec<String>,
}

impl ComponentResolversVisitor {
    fn new(request: ComponentResolverRequest) -> Self {
        let monikers = Vec::new();
        Self { request, monikers }
    }

    fn get_monikers(&self) -> Vec<String> {
        self.monikers.clone()
    }

    fn check_instance(&mut self, instance: &Arc<ComponentInstanceForAnalyzer>) -> Result<()> {
        if let Ok(Some((
            ExtendedInstanceInterface::Component(resolver_register_instance),
            resolver,
        ))) = instance.environment().get_registered_resolver(&self.request.scheme)
        {
            // The resolver is a capability and we need to get the component that provides it.
            let resolver_source = match ComponentModelForAnalyzer::route_capability_sync(
                routing::RouteRequest::Resolver(resolver),
                &resolver_register_instance,
            ) {
                Ok((source, _route)) => {
                    match source {
                        RouteSource::Resolver(resolver) => {
                            match resolver.source_instance().upgrade()? {
                            routing::component_instance::ExtendedInstanceInterface::Component(
                                component,
                            ) => component,
                            routing::component_instance::ExtendedInstanceInterface::AboveRoot(..) => {
                                return Err(anyhow!("The plugin is unable to verify resolvers declared above the root."));
                            }
                        }
                        }
                        _ => {
                            unreachable!("We only care about resolvers!")
                        }
                    }
                }
                Err(err) => {
                    eprintln!(
                        "Ignoring invalid resolver configuration for {}: {:#}",
                        instance.abs_moniker(),
                        anyhow!(err).context("failed to route to a resolver")
                    );
                    return Ok(());
                }
            };

            let moniker = moniker::AbsoluteMoniker::parse_str(&self.request.moniker)?;

            if *resolver_source.abs_moniker() == moniker {
                for use_decl in &resolver_source.decl_for_testing().uses {
                    if let UseDecl::Protocol(name) = use_decl {
                        if name.source_name == CapabilityName(self.request.protocol.clone()) {
                            self.monikers.push(instance.abs_moniker().to_string());
                        }
                    }
                }
            }
        }

        Ok(())
    }
}

impl ComponentInstanceVisitor for ComponentResolversVisitor {
    fn visit_instance(&mut self, instance: &Arc<ComponentInstanceForAnalyzer>) -> Result<()> {
        self.check_instance(instance)
            .with_context(|| format!("while visiting {}", instance.abs_moniker()))
    }
}

impl DataController for ComponentResolversController {
    fn query(&self, model: Arc<DataModel>, request: Value) -> Result<Value> {
        let tree_data = model
            .get::<V2ComponentModel>()
            .context("Failed to get V2ComponentModel from ComponentResolversController model")?;
        let deps = tree_data.deps.clone();
        let controller: ComponentResolverRequest = serde_json::from_value(request)?;

        let model = &tree_data.component_model;

        let mut walker = BreadthFirstModelWalker::new();
        let mut visitor = ComponentResolversVisitor::new(controller);

        walker.walk(&model, &mut visitor).context(
            "Failed to walk V2ComponentModel with BreadthFirstWalker and ComponentResolversVisitor",
        )?;

        Ok(json!(ComponentResolverResponse { monikers: visitor.get_monikers(), deps }))
    }

    fn description(&self) -> String {
        "Finds components resolved by a particular resolver".to_string()
    }

    fn usage(&self) -> String {
        "Finds the component node paths of all components that, in their
environment, contain a resolver with the given moniker for scheme with
access to protocol.

Required parameters:
--scheme:  the resolver URI scheme to query
--moniker: the node path of the resolver
--resolver: filter results to components resolved with a resolver that has access to the given protocol"
            .to_string()
    }
}
