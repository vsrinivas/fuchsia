// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::verify::collection::V2ComponentTree,
    anyhow::{anyhow, Result},
    cm_fidl_analyzer::capability_routing::{
        directory::DirectoryCapabilityRouteVerifier,
        error::CapabilityRouteError,
        protocol::ProtocolCapabilityRouteVerifier,
        route::RouteSegment,
        verifier::{CapabilityRouteVerifier, VerifyRouteResult},
    },
    cm_fidl_analyzer::component_tree::{
        BreadthFirstWalker, ComponentNode, ComponentNodeVisitor, ComponentTree, ComponentTreeWalker,
    },
    cm_rust::CapabilityTypeName,
    log::error,
    scrutiny::{model::controller::DataController, model::model::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::{collections::HashSet, sync::Arc},
};

// TreeMappingController
//
// A placeholder DataController which builds the tree of v2 components and returns its node
// identifiers in breadth-first order.
#[derive(Default)]
pub struct TreeMappingController {}

// A placeholder ComponentNodeVisitor which just records the node path of each node visited,
// together with its component URL.
#[derive(Default)]
struct TreeMappingVisitor {
    pub visited: Vec<Value>,
}

impl ComponentNodeVisitor for TreeMappingVisitor {
    fn visit_node(&mut self, node: &ComponentNode) -> Result<()> {
        self.visited.push(json!({"node": node.short_display(), "url": node.url()}));
        Ok(())
    }
}

impl DataController for TreeMappingController {
    fn query(&self, model: Arc<DataModel>, _value: Value) -> Result<Value> {
        let tree = &model.get::<V2ComponentTree>()?.tree;
        let mut walker = BreadthFirstWalker::new(&tree)?;
        let mut visitor = TreeMappingVisitor::default();
        walker.walk(&tree, &mut visitor)?;

        return Ok(json!({"route": visitor.visited}));
    }

    fn description(&self) -> String {
        "a placeholder analyzer which walks the full v2 component tree and reports its route"
            .to_string()
    }
}

// CapabilityRouteController
//
// A DataController which verifies routes for all capabilities of the specified types.
#[derive(Default)]
pub struct CapabilityRouteController {}

// The expected query format. `capability_types` should be a space-separated list of
// capability types to verify, and `response_level` should be one of "all", "warn", or
// "error".
#[derive(Deserialize, Serialize)]
pub struct CapabilityRouteRequest {
    pub capability_types: String,
    pub response_level: String,
}

// Configures the amount of information that `CapabilityRouteController` returns.
enum ResponseLevel {
    // Only return errors.
    Error,
    // Return errors and warnings.
    Warn,
    // Return errors, warnings, and a summary of each OK route.
    All,
}

// An umbrella type for the capability-type-specific implementations of the
// `CapabilityRouteVerifier` trait.
enum VerifierForCapabilityType {
    Directory(DirectoryCapabilityRouteVerifier),
    Protocol(ProtocolCapabilityRouteVerifier),
}

// A `VerifierForCapabilityType` together with its record of verification results.
struct VerifierWithResults {
    capability_type: CapabilityTypeName,
    verifier: VerifierForCapabilityType,
    results: Vec<VerifyRouteResult>,
}

// A visitor that invokes each of its `verifiers` at each node of `tree`.
struct CapabilityRouteVisitor<'a> {
    tree: &'a ComponentTree,
    verifiers: Vec<VerifierWithResults>,
}

impl VerifierForCapabilityType {
    fn new(capability_type: &CapabilityTypeName) -> Result<Self> {
        match capability_type {
            CapabilityTypeName::Directory => {
                Ok(Self::Directory(DirectoryCapabilityRouteVerifier::new()))
            }
            CapabilityTypeName::Protocol => {
                Ok(Self::Protocol(ProtocolCapabilityRouteVerifier::new()))
            }
            _ => Err(anyhow!(
                "Route verification is not yet implemented for capabilities of type {}",
                capability_type.to_string()
            )),
        }
    }
}

// Used to forward calls to `CapabilityRouteVerifier` trait methods through to the
// inner value of a `VerifierForCapabilityType`.
macro_rules! forward_to_verifier {
    ($value:expr, $inner:pat => $result:expr) => {
        match $value {
            VerifierForCapabilityType::Directory($inner) => $result,
            VerifierForCapabilityType::Protocol($inner) => $result,
        }
    };
}

impl VerifierWithResults {
    fn new(capability_type: &CapabilityTypeName) -> Result<Self> {
        let verifier = VerifierForCapabilityType::new(capability_type)?;
        Ok(Self {
            capability_type: capability_type.clone(),
            verifier,
            results: Vec::<VerifyRouteResult>::new(),
        })
    }

    fn verify(&mut self, tree: &ComponentTree, node: &ComponentNode) {
        let mut results =
            forward_to_verifier!(&self.verifier, v => v.verify_all_routes(tree, node));
        self.results.append(&mut results);
    }

    fn report_ok_routes(&self) -> Value {
        let mut ok_routes = Vec::<Value>::new();

        let ok_results: Vec<&VerifyRouteResult> =
            self.results.iter().filter(|r| r.result.is_ok()).collect();

        for result in ok_results {
            ok_routes.push(json!({"using_node": result.using_node.to_string(),
                                  "capability": result.capability.str(),
                                  "route": Self::format_route(&result.result.as_ref().unwrap())}));
        }

        json!(ok_routes)
    }

    fn report_warnings(&self) -> Value {
        let mut warnings = Vec::<Value>::new();

        let warn_results: Vec<&VerifyRouteResult> = self
            .results
            .iter()
            .filter(|r| {
                r.result.is_err() && Self::report_as_warning(&r.result.as_ref().err().unwrap())
            })
            .collect();

        for result in warn_results {
            warnings.push(json!({"using_node": result.using_node.to_string(),
                                 "capability": result.capability.str(),
                                 "warning": &result.result.as_ref().err().unwrap().to_string()}));
        }

        json!(warnings)
    }

    fn report_errors(&self) -> Value {
        let mut errors = Vec::<Value>::new();

        let err_results: Vec<&VerifyRouteResult> = self
            .results
            .iter()
            .filter(|r| {
                r.result.is_err() && !Self::report_as_warning(&r.result.as_ref().err().unwrap())
            })
            .collect();

        for result in err_results {
            errors.push(json!({"using_node": result.using_node.to_string(),
                               "capability": result.capability.str(),
                               "error": &result.result.as_ref().err().unwrap().to_string()}));
        }

        json!(errors)
    }

    fn report_results(&self, level: &ResponseLevel) -> Value {
        let results: Value;

        match level {
            ResponseLevel::Error => results = json!({"errors": self.report_errors()}),
            ResponseLevel::Warn => {
                results = json!({"errors": self.report_errors(),
                                 "warnings": self.report_warnings()})
            }
            ResponseLevel::All => {
                results = json!({"errors": self.report_errors(),
                                 "warnings": self.report_warnings(),
                                 "ok": self.report_ok_routes()})
            }
        }

        json!({"capability_type": self.capability_type.to_string(), "results": results })
    }

    fn format_route_segment(segment: &RouteSegment) -> Value {
        match segment {
            RouteSegment::UseBy(node, name, source) => {
                json!({"action": "use", "by": node.to_string(),"as": name.str(), "from": source.to_string()})
            }
            RouteSegment::OfferBy(node, name, source) => {
                json!({"action": "offer", "by": node.to_string(), "as": name.str(), "from": source.to_string()})
            }
            RouteSegment::ExposeBy(node, name, source) => {
                json!({"action": "expose", "by": node.to_string(),"as": name.str(), "from": source.to_string()})
            }
            RouteSegment::DeclareBy(node, name) => {
                json!({"action": "declare", "as": name.str(), "by": node.to_string()})
            }
            RouteSegment::RouteFromFramework => {
                json!({"action": "offer", "by": "framework"})
            }
            RouteSegment::RouteFromRootParent => {
                json!({"action": "offer", "by": "root parent"})
            }
        }
    }

    fn format_route(route: &Vec<RouteSegment>) -> Value {
        let mut segments = Vec::<Value>::new();
        for segment in route {
            segments.push(Self::format_route_segment(&segment))
        }
        json!(segments)
    }

    // Selects some error types to report as warnings rather than as errors.
    fn report_as_warning(error: &CapabilityRouteError) -> bool {
        match error {
            // It is expected that some components in a build may have children
            // that are not included in the build.
            CapabilityRouteError::ComponentNotFound(_) => true,
            CapabilityRouteError::ValidationNotImplemented(_) => true,
            _ => false,
        }
    }
}

impl<'a> CapabilityRouteVisitor<'a> {
    fn new(tree: &'a ComponentTree, capability_types: &HashSet<CapabilityTypeName>) -> Self {
        let mut verifiers = Vec::<VerifierWithResults>::new();
        for cap_type in capability_types.iter() {
            match VerifierWithResults::new(cap_type) {
                Ok(verifier) => {
                    verifiers.push(verifier);
                }
                Err(err) => {
                    error!("{}", err.to_string());
                }
            }
        }
        verifiers.sort_by(|a, b| a.capability_type.to_string().cmp(&b.capability_type.to_string()));
        Self { tree, verifiers }
    }

    fn report_results(&self, level: &ResponseLevel) -> Value {
        let mut results = Vec::<Value>::new();
        for verifier in self.verifiers.iter() {
            results.push(verifier.report_results(level));
        }
        Value::Array(results)
    }
}

impl<'a> ComponentNodeVisitor for CapabilityRouteVisitor<'a> {
    fn visit_node(&mut self, node: &ComponentNode) -> Result<()> {
        for verifier in &mut self.verifiers {
            verifier.verify(self.tree, node);
        }
        Ok(())
    }
}

impl CapabilityRouteController {
    fn parse_request(json_request: Value) -> Result<(HashSet<CapabilityTypeName>, ResponseLevel)> {
        let request: CapabilityRouteRequest = serde_json::from_value(json_request)?;

        let mut capability_types = HashSet::<CapabilityTypeName>::new();
        for name in request.capability_types.split(" ") {
            capability_types.insert(Self::parse_capability_type(&name)?);
        }

        let response_level = Self::parse_response_level(&request.response_level)?;
        Ok((capability_types, response_level))
    }

    fn parse_capability_type(name: &str) -> Result<CapabilityTypeName> {
        match name {
            "directory" => Ok(CapabilityTypeName::Directory),
            "event" => Ok(CapabilityTypeName::Event),
            "event_stream" => Ok(CapabilityTypeName::EventStream),
            "protocol" => Ok(CapabilityTypeName::Protocol),
            "resolver" => Ok(CapabilityTypeName::Resolver),
            "runner" => Ok(CapabilityTypeName::Runner),
            "service" => Ok(CapabilityTypeName::Service),
            "storage" => Ok(CapabilityTypeName::Storage),
            _ => Err(anyhow!("unrecognized capability type name {}", name)),
        }
    }

    fn parse_response_level(level: &str) -> Result<ResponseLevel> {
        match level {
            "all" => Ok(ResponseLevel::All),
            "warn" => Ok(ResponseLevel::Warn),
            "error" => Ok(ResponseLevel::Error),
            _ => Err(anyhow!("unrecognized response level {}", level)),
        }
    }
}

impl DataController for CapabilityRouteController {
    fn query(&self, model: Arc<DataModel>, request: Value) -> Result<Value> {
        let (capability_types, response_level) = Self::parse_request(request)?;
        let tree = &model.get::<V2ComponentTree>()?.tree;
        let mut walker = BreadthFirstWalker::new(&tree)?;
        let mut visitor = CapabilityRouteVisitor::new(&tree, &capability_types);
        walker.walk(&tree, &mut visitor)?;
        Ok(visitor.report_results(&response_level))
    }

    fn description(&self) -> String {
        "verifies v2 capability routes".to_string()
    }

    fn usage(&self) -> String {
        "Verifies routing for each capability that is used by some v2 component.
         Walks each route from the using component to the final source, checking
         that each handoff is valid.

         Required parameters:
         --capability_types: a space-separated list of capability types to verify.
         --response_level: one of `error` (return errors only), `warn` (return errors
                           and warnings) and `all` (return errors, warnings, and a 
                           summary of each valid route)."
            .to_string()
    }
}
