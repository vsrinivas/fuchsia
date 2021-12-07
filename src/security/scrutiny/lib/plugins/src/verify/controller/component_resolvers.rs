// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::verify::collection::V2ComponentModel,
    anyhow::{Context, Result},
    cm_fidl_analyzer::{
        component_instance::ComponentInstanceForAnalyzer, BreadthFirstModelWalker,
        ComponentInstanceVisitor, ComponentModelWalker,
    },
    cm_rust::{CapabilityName, RegistrationSource, UseDecl},
    moniker::{AbsoluteMonikerBase, PartialChildMoniker},
    routing::component_instance::{
        ComponentInstanceInterface, ExtendedInstanceInterface, ResolvedInstanceInterface,
    },
    scrutiny::{model::controller::DataController, model::model::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

// ComponentResolversController
//
// A DataController which returns a list of components that can be resolved given a scheme, protocol, and moniker.
#[derive(Default)]
pub struct ComponentResolversController {}

// The expected query format.
#[derive(Deserialize, Serialize)]
pub struct ComponentResolverRequest {
    pub scheme: String,
    pub moniker: String,
    pub protocol: String,
}

// A visitor that queries the tree for component node paths given a request.
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
}

impl ComponentInstanceVisitor for ComponentResolversVisitor {
    fn visit_instance(&mut self, instance: &Arc<ComponentInstanceForAnalyzer>) -> Result<()> {
        if let Ok(Some((
            ExtendedInstanceInterface::Component(resolver_parent_instance),
            resolver,
        ))) = instance.environment().get_registered_resolver(&self.request.scheme)
        {
            let resolver_instance = {
                match &resolver.source {
                    RegistrationSource::Parent => todo!("fxbug.dev/89882"),
                    RegistrationSource::Self_ => todo!("fxbug.dev/89882"),
                    RegistrationSource::Child(moniker) => resolver_parent_instance
                        .get_live_child(&PartialChildMoniker::new(moniker.to_owned(), None))
                        .unwrap(),
                }
            };

            let moniker =
                moniker::AbsoluteMoniker::parse_string_without_instances(&self.request.moniker)?
                    .to_string();

            if resolver_instance.abs_moniker().to_string() == moniker {
                for use_decl in &resolver_instance.decl_for_testing().uses {
                    if let UseDecl::Protocol(name) = use_decl {
                        if name.source_name == CapabilityName(self.request.protocol.clone()) {
                            let moniker = instance.abs_moniker();
                            self.monikers.push(moniker.to_string());
                        }
                    }
                }
            }
        }

        Ok(())
    }
}

impl DataController for ComponentResolversController {
    fn query(&self, model: Arc<DataModel>, request: Value) -> Result<Value> {
        let tree_data = model
            .get::<V2ComponentModel>()
            .context("Failed to get V2ComponentModel from ComponentResolversController model")?;
        let controller: ComponentResolverRequest = serde_json::from_value(request)?;

        let model = &tree_data.component_model;

        let mut walker = BreadthFirstModelWalker::new();
        let mut visitor = ComponentResolversVisitor::new(controller);

        walker.walk(&model, &mut visitor).context(
            "Failed to walk V2ComponentModel with BreadthFirstWalker and ComponentResolversVisitor",
        )?;

        let monikers = visitor.get_monikers();
        Ok(json!(monikers))
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
