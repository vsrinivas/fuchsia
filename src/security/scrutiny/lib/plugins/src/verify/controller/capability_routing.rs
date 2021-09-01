// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::verify::{
        collection::V2ComponentTree, ErrorResult, OkResult, ResultsBySeverity,
        ResultsForCapabilityType, WarningResult,
    },
    anyhow::{anyhow, Context, Result},
    cm_fidl_analyzer::capability_routing::{
        directory::DirectoryCapabilityRouteVerifier,
        error::CapabilityRouteError,
        protocol::ProtocolCapabilityRouteVerifier,
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

/// Generic verification result type. Variants implement serialization.
enum ResultBySeverity {
    Error(ErrorResult),
    Warning(WarningResult),
    Ok(OkResult),
}

impl From<VerifyRouteResult> for ResultBySeverity {
    fn from(verify_route_result: VerifyRouteResult) -> Self {
        match verify_route_result.result {
            Ok(route) => OkResult {
                using_node: verify_route_result.using_node,
                capability: verify_route_result.capability,
                route,
            }
            .into(),
            Err(error) => {
                match error {
                    // It is expected that some components in a build may have
                    // children that are not included in the build.
                    CapabilityRouteError::ComponentNotFound(_)
                    | CapabilityRouteError::ValidationNotImplemented(_) => WarningResult {
                        using_node: verify_route_result.using_node,
                        capability: verify_route_result.capability,
                        warning: error.into(),
                    }
                    .into(),
                    _ => ErrorResult {
                        using_node: verify_route_result.using_node,
                        capability: verify_route_result.capability,
                        error: error.into(),
                    }
                    .into(),
                }
            }
        }
    }
}

impl From<OkResult> for ResultBySeverity {
    fn from(ok_result: OkResult) -> Self {
        Self::Ok(ok_result)
    }
}

impl From<WarningResult> for ResultBySeverity {
    fn from(warning_result: WarningResult) -> Self {
        Self::Warning(warning_result)
    }
}

impl From<ErrorResult> for ResultBySeverity {
    fn from(error_result: ErrorResult) -> Self {
        Self::Error(error_result)
    }
}

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
    // Same as `All`; also include unstable `cm_rust` types.
    Verbose,
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

    fn split_ok_warn_error_results(&self) -> (Vec<OkResult>, Vec<WarningResult>, Vec<ErrorResult>) {
        let mut ok_results = vec![];
        let mut warning_results = vec![];
        let mut error_results = vec![];

        for result in self.results.iter() {
            match result.clone().into() {
                ResultBySeverity::Ok(ok_result) => ok_results.push(ok_result),
                ResultBySeverity::Warning(warning_result) => warning_results.push(warning_result),
                ResultBySeverity::Error(error_result) => error_results.push(error_result),
            }
        }
        (ok_results, warning_results, error_results)
    }

    fn report_results(&self, level: &ResponseLevel) -> ResultsForCapabilityType {
        let (ok, warnings, errors) = self.split_ok_warn_error_results();
        let results = match level {
            ResponseLevel::Error => ResultsBySeverity { errors, ..Default::default() },
            ResponseLevel::Warn => ResultsBySeverity { errors, warnings, ..Default::default() },
            ResponseLevel::All => ResultsBySeverity {
                errors,
                warnings,
                ok: ok
                    .into_iter()
                    .map(|result| OkResult {
                        // `All` response level omits route details that depend on an
                        // unstable route details format.
                        route: vec![],
                        ..result
                    })
                    .collect(),
            },
            ResponseLevel::Verbose => ResultsBySeverity { errors, warnings, ok },
        };
        ResultsForCapabilityType { capability_type: self.capability_type.clone(), results }
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

    fn report_results(&self, level: &ResponseLevel) -> Vec<ResultsForCapabilityType> {
        self.verifiers.iter().map(|verifier| verifier.report_results(level)).collect()
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
            "verbose" => Ok(ResponseLevel::Verbose),
            "all" => Ok(ResponseLevel::All),
            "warn" => Ok(ResponseLevel::Warn),
            "error" => Ok(ResponseLevel::Error),
            _ => Err(anyhow!("unrecognized response level {}", level)),
        }
    }
}

impl DataController for CapabilityRouteController {
    fn query(&self, model: Arc<DataModel>, request: Value) -> Result<Value> {
        let (capability_types, response_level) = Self::parse_request(request)
            .context("Failed to parse CapabilityRouteController request")?;
        let tree = &model
            .get::<V2ComponentTree>()
            .context("Failed to get V2ComponentTree from CapabilityRouteController model")?
            .tree;
        let mut walker = BreadthFirstWalker::new(&tree)
            .context("Failed to initialize BreadthFirstWalker from V2ComponentTree")?;
        let mut visitor = CapabilityRouteVisitor::new(&tree, &capability_types);
        walker.walk(&tree, &mut visitor).context(
            "Failed to walk V2ComponentTree with BreadthFirstWalker and CapabilityRouteVisitor",
        )?;
        let results = visitor.report_results(&response_level);
        Ok(json!(results))
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
         --response_level: one of `error` (return errors only); `warn` (return
                           errors and warnings); `all` (return errors, warnings,
                           and a list of capabilities with valid routes); or
                           `verbose` (same as `all`, but with detailed route
                           information). Note that the format for `verbose`
                           output is unstable; external tools should not rely on
                           the output format."
            .to_string()
    }
}
