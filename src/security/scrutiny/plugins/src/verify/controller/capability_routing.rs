// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::verify::{
        collection::V2ComponentModel, CapabilityRouteResults, ErrorResult, OkResult,
        ResultsBySeverity, ResultsForCapabilityType, WarningResult,
    },
    anyhow::{anyhow, Context, Result},
    cm_fidl_analyzer::{
        component_instance::ComponentInstanceForAnalyzer,
        component_model::{AnalyzerModelError, ComponentModelForAnalyzer},
        route::{CapabilityRouteError, VerifyRouteResult},
        BreadthFirstModelWalker, ComponentInstanceVisitor, ComponentModelWalker,
        ModelMappingVisitor,
    },
    cm_rust::CapabilityTypeName,
    routing::error::{ComponentInstanceError, RoutingError},
    scrutiny::{model::controller::DataController, model::model::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::{
        collections::{HashMap, HashSet},
        sync::Arc,
    },
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
                    | CapabilityRouteError::ValidationNotImplemented(_)
                    | CapabilityRouteError::AnalyzerModelError(
                        AnalyzerModelError::ComponentInstanceError(
                            ComponentInstanceError::InstanceNotFound { .. },
                        ),
                    )
                    | CapabilityRouteError::AnalyzerModelError(AnalyzerModelError::RoutingError(
                        RoutingError::EnvironmentFromChildInstanceNotFound { .. },
                    ))
                    | CapabilityRouteError::AnalyzerModelError(AnalyzerModelError::RoutingError(
                        RoutingError::ExposeFromChildInstanceNotFound { .. },
                    ))
                    | CapabilityRouteError::AnalyzerModelError(AnalyzerModelError::RoutingError(
                        RoutingError::OfferFromChildInstanceNotFound { .. },
                    ))
                    | CapabilityRouteError::AnalyzerModelError(AnalyzerModelError::RoutingError(
                        RoutingError::UseFromChildInstanceNotFound { .. },
                    )) => WarningResult {
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

// V2ComponentModelMappingController
//
// A DataController which builds the tree of v2 components and lists all component instance identifiers
// in breadth-first order.
#[derive(Default)]
pub struct V2ComponentModelMappingController {}

impl DataController for V2ComponentModelMappingController {
    fn query(&self, model: Arc<DataModel>, _value: Value) -> Result<Value> {
        let component_model = Arc::clone(
            &model
                .get::<V2ComponentModel>()
                .context("Failed to get V2ComponentModel from CapabilityRouteController model")?
                .component_model,
        );
        let mut walker = BreadthFirstModelWalker::new();
        let mut visitor = ModelMappingVisitor::default();
        walker.walk(&component_model, &mut visitor)?;

        let mut instances = Vec::new();
        for (instance, url) in visitor.map().iter() {
            instances.push(json!({ "instance": instance.clone(), "url": url.clone()}));
        }
        Ok(json!({ "instances": instances }))
    }

    fn description(&self) -> String {
        "an analyzer that walks the full v2 component tree and reports all instance IDs in breadth-first order"
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

// A visitor that checks the route for each capability in `model` whose type appears in `capability_types`.
struct CapabilityRouteVisitor {
    model: Arc<ComponentModelForAnalyzer>,
    capability_types: HashSet<CapabilityTypeName>,
    results: HashMap<CapabilityTypeName, Vec<VerifyRouteResult>>,
}

impl CapabilityRouteVisitor {
    fn new(
        model: Arc<ComponentModelForAnalyzer>,
        capability_types: HashSet<CapabilityTypeName>,
    ) -> Self {
        let mut results = HashMap::new();
        for type_name in capability_types.iter() {
            results.insert(type_name.clone(), vec![]);
        }
        Self { model, capability_types, results }
    }

    fn split_ok_warn_error_results(
        &self,
    ) -> HashMap<CapabilityTypeName, (Vec<OkResult>, Vec<WarningResult>, Vec<ErrorResult>)> {
        let mut split_results = HashMap::new();
        for (type_name, type_results) in self.results.iter() {
            let mut ok_results = vec![];
            let mut warning_results = vec![];
            let mut error_results = vec![];

            for result in type_results.iter() {
                match result.clone().into() {
                    ResultBySeverity::Ok(ok_result) => ok_results.push(ok_result),
                    ResultBySeverity::Warning(warning_result) => {
                        warning_results.push(warning_result)
                    }
                    ResultBySeverity::Error(error_result) => error_results.push(error_result),
                }
            }
            split_results.insert(type_name.clone(), (ok_results, warning_results, error_results));
        }
        split_results
    }

    fn report_results(&self, level: &ResponseLevel) -> Vec<ResultsForCapabilityType> {
        let mut filtered_results = Vec::new();
        let split_results = self.split_ok_warn_error_results();
        for (type_name, (ok, warnings, errors)) in split_results.into_iter() {
            let filtered_for_type = match level {
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
            filtered_results.push(ResultsForCapabilityType {
                capability_type: type_name.clone(),
                results: filtered_for_type,
            })
        }
        filtered_results
            .sort_by(|r, s| r.capability_type.to_string().cmp(&s.capability_type.to_string()));
        filtered_results
    }
}

impl ComponentInstanceVisitor for CapabilityRouteVisitor {
    fn visit_instance(&mut self, instance: &Arc<ComponentInstanceForAnalyzer>) -> Result<()> {
        let check_results = self.model.check_routes_for_instance(instance, &self.capability_types);
        for (type_name, results) in check_results.into_iter() {
            let type_results =
                self.results.get_mut(&type_name).expect("expected results for capability type");
            for result in results.into_iter() {
                type_results.push(result);
            }
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
        let component_model_data = Arc::clone(
            &model
                .get::<V2ComponentModel>()
                .context("Failed to get V2ComponentModel from CapabilityRouteController model")?,
        );
        let deps = component_model_data.deps.clone();
        let component_model = &component_model_data.component_model;
        let mut walker = BreadthFirstModelWalker::new();
        let mut visitor =
            CapabilityRouteVisitor::new(Arc::clone(component_model), capability_types);
        walker.walk(&component_model, &mut visitor).context(
            "Failed to walk V2ComponentModel with BreadthFirstModelWalker and CapabilityRouteVisitor",
        )?;
        let results = visitor.report_results(&response_level);
        Ok(json!(CapabilityRouteResults { deps, results }))
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
