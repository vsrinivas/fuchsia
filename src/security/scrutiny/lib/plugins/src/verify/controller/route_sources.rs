// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::verify::collection::V2ComponentTree,
    anyhow::{anyhow, Context, Error, Result},
    cm_fidl_analyzer::{
        capability_routing::{
            directory::DirectoryCapabilityRouteVerifier, error::CapabilityRouteError,
            route::RouteSegment, verifier::CapabilityRouteVerifier,
        },
        component_tree::{ComponentNode, ComponentTree, NodePath},
    },
    cm_rust::{
        CapabilityDecl, CapabilityName, CapabilityPath, CapabilityTypeName, ComponentDecl,
        ExposeDecl, OfferDecl, UseDecl,
    },
    scrutiny::model::{controller::DataController, model::DataModel},
    serde::{Deserialize, Serialize},
    serde_json::{self, json, value::Value},
    std::{collections::HashMap, fmt, path::PathBuf, str::FromStr, sync::Arc},
};

const BAD_REQUEST_CTX: &str = "Failed to parse RouteSourcesController request";
const MISSING_TARGET_NODE: &str = "Target node is missing from component tree";
const GATHER_FAILED: &str = "Target node failed to gather routes_to_skip and routes_to_verify";
const ROUTE_LISTS_INCOMPLETE: &str = "Component route skip list + verify list incomplete";
const ROUTE_LISTS_OVERLAP: &str = "Component route skip list + verify list contains duplicates";
const MATCH_ONE_FAILED: &str = "Failed to match exactly one item";
const USE_SPEC_NAME: &str = "use spec";
const USE_DECL_NAME: &str = "use declaration";

/// Content of a request to a `RouteSourcesController`, containing an exhuastive
/// list of routes to zero or more component instances.
#[derive(Deserialize, Serialize)]
pub struct RouteSourcesRequest {
    pub component_routes: Vec<RouteSourcesSpec>,
}

/// Specification of all routes to a component instance in the component tree.
/// Each route must be listed, either to be verified or skipped by the verifier.
#[derive(Deserialize, Serialize)]
pub struct RouteSourcesSpec {
    /// Absolute path to the component instance whose routes are to be verified.
    pub target_node_path: NodePath,
    /// Routes that are expected to be present, but do not require verification.
    pub routes_to_skip: Vec<UseSpec>,
    /// Route specification and route source matching information for routes
    /// that are to be verified.
    pub routes_to_verify: Vec<RouteMatch>,
}

/// Input query type for matching routes by target. Usually, either a `path` or
/// a `name` is specified.
#[derive(Deserialize, Serialize)]
pub struct UseSpec {
    /// Route capability types.
    #[serde(rename = "use_type")]
    pub type_name: CapabilityTypeName,
    /// Target capability path the match, if any.
    #[serde(rename = "use_path")]
    pub path: Option<CapabilityPath>,
    /// Target capability name, if any.
    #[serde(rename = "use_name")]
    pub name: Option<CapabilityName>,
}

/// Match a `UseDecl` to a `UseSpec` when types match and spec'd `name` and/or
/// `path` matches.
impl Matches<UseDecl> for UseSpec {
    const NAME: &'static str = USE_SPEC_NAME;
    const OTHER_NAME: &'static str = USE_DECL_NAME;

    fn matches(&self, other: &UseDecl) -> Result<bool> {
        Ok(self.type_name == other.into()
            && self.path.as_ref() == other.path()
            && self.name.as_ref() == other.name())
    }
}

/// Input query type for matching routes by target and source.
#[derive(Deserialize, Serialize)]
pub struct RouteMatch {
    /// Route information to match an entry in the component instance `uses`
    /// array.
    #[serde(flatten)]
    target: UseSpec,
    /// Route source information to match the capability declaration at the
    /// route's source.
    #[serde(flatten)]
    source: SourceSpec,
}

/// Input query type for matching a route source.
#[derive(Deserialize, Serialize)]
pub struct SourceSpec {
    /// Node path prefix expected at the source node.
    #[serde(rename = "source_node_path")]
    node_path: NodePath,
    /// Capability declaration expected at the source node.
    #[serde(flatten)]
    capability: SourceDeclSpec,
}

/// Input query type for matching a capability declaration. Usually, either a
// `path` or a `name` is specified.
#[derive(Deserialize, Serialize)]
pub struct SourceDeclSpec {
    /// Path designated in capability declaration, if any.
    #[serde(rename = "source_path_prefix")]
    pub path_prefix: Option<CapabilityPath>,
    /// Name designated in capability declaration, if any.
    #[serde(rename = "source_name")]
    pub name: Option<CapabilityName>,
}

/// Match a `CapabilityDecl` to a `SourceDeclSpec`. Only the `name` is matched because matching
/// `path_prefix` requires complete capability route.
impl Matches<CapabilityDecl> for SourceDeclSpec {
    const NAME: &'static str = "source declaration spec";
    const OTHER_NAME: &'static str = "capability declaration";

    fn matches(&self, other: &CapabilityDecl) -> Result<bool> {
        Ok(match &self.name {
            Some(name) => name == other.name(),
            None => true,
        })
    }
}

/// Match a complete route against to a `SourceDeclSpec` by following `subdir`
/// designations along route and delegating to `Matches<CapabilityDecl>`
/// implementation.
impl Matches<Vec<RouteSegment>> for SourceDeclSpec {
    const NAME: &'static str = "source declaration spec";
    const OTHER_NAME: &'static str = "route";

    fn matches(&self, other: &Vec<RouteSegment>) -> Result<bool> {
        // Spec cannot match empty route.
        let decl = other.last();
        if decl.is_none() {
            return Ok(false);
        }
        let decl = decl.unwrap();

        // Accumulate `subdir` onto `decl.capability.source_path`, where applicable, and delegate
        // to `self.matches(&CapabilityDecl)`.
        match decl {
            RouteSegment::DeclareBy { capability, .. } => match &capability {
                CapabilityDecl::Directory(decl) => {
                    if self.path_prefix.is_none() || decl.source_path.is_none() {
                        return Ok(false);
                    }
                    let path_prefix = self.path_prefix.as_ref().unwrap();
                    let source_path = &decl.source_path.as_ref().unwrap();
                    let subdirs = get_subdirs(other);
                    let source_path_str = subdirs.iter().fold(source_path.to_path_buf(), |path_buf, next| {
                            let mut next_buf = path_buf.clone();
                            next_buf.push(next);
                            next_buf
                        }).to_str().ok_or_else(|| anyhow!("Failed to format PathBuf as string; components; {:?} appended with {:?}", decl.source_path, subdirs))?.to_string();
                    let source_path =
                        CapabilityPath::from_str(&source_path_str).with_context(|| {
                            anyhow!(
                                "Failed to parse string into CapabilityPath: {}",
                                source_path_str
                            )
                        })?;

                    Ok(match_path_prefix(path_prefix, &source_path))
                }
                _ => Ok(false),
            },
            _ => Ok(false),
        }
    }
}

fn get_subdirs(route: &Vec<RouteSegment>) -> Vec<PathBuf> {
    let mut subdir = vec![];
    for segment in route.iter() {
        match segment {
            RouteSegment::UseBy { capability, .. } => match capability {
                UseDecl::Directory(decl) => {
                    if let Some(decl_subdir) = &decl.subdir {
                        subdir.push(decl_subdir.clone());
                    }
                }
                _ => {}
            },
            RouteSegment::OfferBy { capability, .. } => match capability {
                OfferDecl::Directory(decl) => {
                    if let Some(decl_subdir) = &decl.subdir {
                        subdir.push(decl_subdir.clone());
                    }
                }
                _ => {}
            },
            RouteSegment::ExposeBy { capability, .. } => match capability {
                ExposeDecl::Directory(decl) => {
                    if let Some(decl_subdir) = &decl.subdir {
                        subdir.push(decl_subdir.clone());
                    }
                }
                _ => {}
            },
            RouteSegment::DeclareBy { capability, .. } => match capability {
                CapabilityDecl::Storage(decl) => {
                    if let Some(decl_subdir) = &decl.subdir {
                        subdir.push(decl_subdir.clone());
                    }
                }
                _ => {}
            },
            _ => {}
        }
    }

    // Subdirs collected target->source, but are applied source->target.
    subdir.reverse();

    subdir
}

fn match_path_prefix(prefix: &CapabilityPath, path: &CapabilityPath) -> bool {
    let prefix = prefix.split();
    let path = path.split();
    if prefix.len() > path.len() {
        return false;
    }
    for (i, expected_segment) in prefix.iter().enumerate() {
        let actual_segment = &path[i];
        if expected_segment != actual_segment {
            return false;
        }
    }

    true
}

/// Output type: The source of a capability route.
#[derive(Debug, Serialize)]
struct Source {
    /// The node path of the declaring component instance.
    node_path: NodePath,
    /// The capability declaration.
    capability: CapabilityDecl,
}

/// Output type: The result of matching a route use+source specification against
/// routes in the component tree.
#[derive(Serialize)]
struct VerifyComponentRouteResult<'a> {
    query: &'a RouteMatch,
    result: Result<Source, ComponentRouteError>,
}

/// Output type: Structured errors for matching a route+use specification
/// against routes in the component tree.
#[derive(Serialize)]
enum ComponentRouteError {
    CapabilityRouteError(CapabilityRouteError),
    RouteMismatch(Source),
    MissingSourceCapability(RouteSegment),
}

/// Intermediate value for use declarations that match a use+source spec.
struct Binding<'a> {
    route_match: &'a RouteMatch,
    use_decl: &'a UseDecl,
}

/// Trait for specifying a match between implementer and `Other` type values,
/// and matching exactly one `Other` from a vector. Matching functions may
/// return an error when input processing operations fail unexpectedly.
trait Matches<Other>: Sized + Serialize
where
    Other: fmt::Debug,
{
    const NAME: &'static str;
    const OTHER_NAME: &'static str;

    fn matches(&self, other: &Other) -> Result<bool>;

    fn match_one<'a>(&self, other: &'a Vec<Other>) -> Result<&'a Other> {
        // Ignore match errors: Only interested in number of positive matches.
        let matches: Vec<&'a Other> = other
            .iter()
            .filter_map(|other| match self.matches(other) {
                Ok(true) => Some(other),
                _ => None,
            })
            .collect();

        if matches.len() == 0 {
            return Err(anyhow!(
                "{}; no {}: {} in {} list: {:#?}",
                MATCH_ONE_FAILED,
                Self::NAME,
                json_or_unformatted(self, Self::NAME),
                Self::OTHER_NAME,
                other,
            ));
        } else if matches.len() > 1 {
            return Err(anyhow!(
                "Multiple instances of {} in {:?} matches {} {:?}; matches: {:?}",
                Self::OTHER_NAME,
                other,
                Self::NAME,
                json_or_unformatted(self, Self::NAME),
                matches
            ));
        }

        Ok(matches[0])
    }
}

/// Container for delegate capability route verifiers, organized by capability
/// type at capability use site.
struct Verifiers {
    directory: DirectoryCapabilityRouteVerifier,
}

impl Verifiers {
    fn new() -> Self {
        Verifiers { directory: DirectoryCapabilityRouteVerifier::new() }
    }

    fn verify_route<'a>(
        &'a self,
        tree: &'a ComponentTree,
        use_decl: &'a UseDecl,
        using_node: &'a ComponentNode,
        use_spec: &'a UseSpec,
    ) -> Result<Result<Vec<RouteSegment>, CapabilityRouteError>> {
        match use_decl {
            UseDecl::Directory(dir_use) => {
                Ok(self.directory.verify_route(tree, dir_use, using_node))
            }
            _ => Err(anyhow!(
                "No verifiers are implemented for capapability use of the form {}",
                json_or_unformatted(use_spec, "capability use declaration")
            )),
        }
    }
}

/// Helper for attempting to embed JSON in error messages.
fn json_or_unformatted<S>(value: &S, type_name: &str) -> String
where
    S: Serialize,
{
    serde_json::to_string_pretty(value).unwrap_or_else(|_| format!("<unformatted {}>", type_name))
}

/// Attempt to gather exactly one match for all `routes_to_skip` and all
/// `routes_to_verify` in `component_routes`.
fn gather_routes<'a>(
    component_routes: &'a RouteSourcesSpec,
    component_decl: &'a ComponentDecl,
    node_path: &'a NodePath,
) -> Result<(Vec<&'a UseDecl>, Vec<Binding<'a>>)> {
    let uses = &component_decl.uses;
    let routes_to_skip = component_routes
        .routes_to_skip
        .iter()
        .map(|use_spec| use_spec.match_one(uses))
        .collect::<Result<Vec<&UseDecl>>>()?;
    let routes_to_verify = component_routes
        .routes_to_verify
        .iter()
        .map(|route_match| {
            route_match.target.match_one(uses).map(|use_decl| Binding { route_match, use_decl })
        })
        .collect::<Result<Vec<Binding<'a>>>>()?;

    let mut all_matches = routes_to_skip.clone();
    all_matches.extend(routes_to_verify.iter().map(|binding| binding.use_decl));
    let mut deduped_matches: Vec<&UseDecl> = vec![];
    let mut duped_matches: Vec<&UseDecl> = vec![];
    for i in 0..all_matches.len() {
        if duped_matches.contains(&all_matches[i]) {
            continue;
        }
        for j in i + 1..all_matches.len() {
            if all_matches[i] == all_matches[j] {
                duped_matches.push(all_matches[i]);
                continue;
            }
        }
        deduped_matches.push(all_matches[i]);
    }

    if deduped_matches.len() < uses.len() {
        let missed_routes: Vec<&UseDecl> =
            uses.iter().filter(|use_decl| !deduped_matches.contains(use_decl)).collect();
        if duped_matches.len() > 0 {
            Err(anyhow!(
                "{}. {}; component node path: {} routes matched multiple times: {:#?}; unmatched routes: {:#?}",
                ROUTE_LISTS_OVERLAP,
                ROUTE_LISTS_INCOMPLETE,
                node_path,
                duped_matches,
                missed_routes
            ))
        } else {
            Err(anyhow!(
                "{}; component node path: {}; unmatched routes: {:#?}",
                ROUTE_LISTS_INCOMPLETE,
                node_path,
                missed_routes
            ))
        }
    } else if duped_matches.len() > 0 {
        Err(anyhow!(
            "{}; component node path: {}; routes matched multiple times: {:#?}",
            ROUTE_LISTS_OVERLAP,
            node_path,
            duped_matches
        ))
    } else {
        Ok((routes_to_skip, routes_to_verify))
    }
}

/// `DataController` for verifying specific routes used by specific components.
#[derive(Default)]
pub struct RouteSourcesController {}

impl DataController for RouteSourcesController {
    fn query(&self, model: Arc<DataModel>, request: Value) -> Result<Value, Error> {
        let request: RouteSourcesRequest =
            serde_json::from_value(request).context(BAD_REQUEST_CTX)?;
        let tree = &model.get::<V2ComponentTree>()?.tree;
        let verifiers = Verifiers::new();

        let mut results = HashMap::new();
        for component_routes in request.component_routes.iter() {
            let target_node_path = &component_routes.target_node_path;
            let target_node = tree.get_node(target_node_path).context(format!(
                "{}; target node: {}",
                MISSING_TARGET_NODE,
                target_node_path.clone()
            ))?;

            let (_, routes_to_verify) =
                gather_routes(component_routes, &target_node.decl, target_node_path).context(
                    format!("{}; target node: {}", GATHER_FAILED, target_node_path.clone()),
                )?;

            let mut component_results = Vec::new();
            for route in routes_to_verify.into_iter() {
                let verify_result = verifiers.verify_route(
                    tree,
                    route.use_decl,
                    target_node,
                    &route.route_match.target,
                )?;
                let query = route.route_match;
                let component_result = match verify_result {
                    Ok(route_details) => {
                        let route_source = route_details.last().ok_or_else(|| {
                            anyhow!(
                                "Route verifier traced empty route for capability matching {:?}",
                                json_or_unformatted(&route.route_match.target, "route target")
                            )
                        })?;
                        if let RouteSegment::DeclareBy { node_path, capability } = route_source {
                            let source = Source {
                                node_path: node_path.clone(),
                                capability: capability.clone(),
                            };
                            let matches_result: Result<Vec<bool>> = vec![
                                route.route_match.source.capability.matches(capability),
                                route.route_match.source.capability.matches(&route_details),
                            ]
                            .into_iter()
                            .map(|r| r)
                            .collect();
                            match matches_result {
                                Ok(source_and_cap_res) => {
                                    if source_and_cap_res[0] && source_and_cap_res[1] {
                                        VerifyComponentRouteResult { query, result: Ok(source) }
                                    } else {
                                        VerifyComponentRouteResult {
                                            query,
                                            result: Err(ComponentRouteError::RouteMismatch(source)),
                                        }
                                    }
                                }
                                Err(_) => VerifyComponentRouteResult {
                                    query,
                                    result: Err(ComponentRouteError::RouteMismatch(source)),
                                },
                            }
                        } else {
                            VerifyComponentRouteResult {
                                query,
                                result: Err(ComponentRouteError::MissingSourceCapability(
                                    route_source.clone(),
                                )),
                            }
                        }
                    }
                    Err(err) => VerifyComponentRouteResult {
                        query,
                        result: Err(ComponentRouteError::CapabilityRouteError(err)),
                    },
                };
                component_results.push(component_result);
            }

            results.insert(format!("/{}", target_node_path.as_vec().join("/")), component_results);
        }

        Ok(json!({ "results": results }))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            RouteMatch, RouteSourcesController, RouteSourcesRequest, RouteSourcesSpec, Source,
            SourceDeclSpec, SourceSpec, UseSpec, VerifyComponentRouteResult, BAD_REQUEST_CTX,
            MATCH_ONE_FAILED, MISSING_TARGET_NODE, ROUTE_LISTS_INCOMPLETE, ROUTE_LISTS_OVERLAP,
            USE_DECL_NAME, USE_SPEC_NAME,
        },
        crate::verify::collection::V2ComponentTree,
        anyhow::{anyhow, Result},
        cm_fidl_analyzer::component_tree::{ComponentTreeBuilder, NodePath},
        cm_rust::{
            CapabilityName, CapabilityPath, CapabilityTypeName, ChildDecl, ComponentDecl,
            DependencyType, DirectoryDecl, ExposeDirectoryDecl, ExposeSource, ExposeTarget,
            OfferDirectoryDecl, OfferSource, OfferTarget, UseDirectoryDecl, UseSource,
        },
        fidl_fuchsia_io2 as fio2, fidl_fuchsia_sys2 as fsys,
        maplit::hashmap,
        scrutiny::prelude::{DataController, DataModel},
        scrutiny_testing::fake::fake_data_model,
        serde_json::json,
        std::{path::PathBuf, str::FromStr, sync::Arc},
    };

    macro_rules! ok_unwrap {
        ($actual_result:expr) => {{
            let actual_result = $actual_result;
            let actual_ref = actual_result.as_ref();
            if actual_ref.is_err() {
                println!("Unexpected Err");
                for err in actual_ref.err().unwrap().chain() {
                    println!("    {}", err.to_string());
                }
            }
            actual_result.ok().unwrap()
        }};
    }

    macro_rules! err_unwrap {
        ($actual_result:expr) => {{
            let actual_result = $actual_result;
            let actual_ref = actual_result.as_ref();
            if actual_ref.is_ok() {
                println!("Unexpected Ok");
                println!("    {:#?}", actual_ref.ok().unwrap());
            }
            actual_result.err().unwrap()
        }};
    }

    macro_rules! err_starts_with {
        ($err:expr, $prefix:expr) => {{
            if !$err.root_cause().to_string().starts_with($prefix) {
                println!("Error root cause does not start with \"{}\"", $prefix);
                for e in $err.chain() {
                    println!("    {}", e.to_string());
                }
            }
            assert!($err.root_cause().to_string().starts_with($prefix));
        }};
    }

    macro_rules! err_contains {
        ($err:expr, $substr:expr) => {{
            if !$err.root_cause().to_string().contains($substr) {
                println!("Error root cause does not contain \"{}\"", $substr);
                for e in $err.chain() {
                    println!("    {}", e.to_string());
                }
            }
            assert!($err.root_cause().to_string().contains($substr));
        }};
    }

    // Component tree:
    //
    //   ________root_url________
    //  /                        \
    // two_dir_user_url           one_dir_provider_url
    // + root_url child name:     + root_url child name:
    //     two_dir_user               one_dir_provider
    //
    // Directory routes:
    //
    // - Component URLs prefixed by @
    // - Capability prefixed by $
    // - Directories and subdirectories inside ()
    //   - Subdirectories have no leading /
    //
    // @one_dir_provider_url: $provider_dir(/data/to/user)
    //     -- $exposed_by_provider(provider_subdir) -->
    //   @root_url
    //     -- $routed_from_provider(root_subdir) -->
    //   @two_dir_user_url
    //     -- (user_subdir) --> (/data/from/provider)
    //
    // I.e.,
    //   @one_dir_provider_url(/data/to/user/provider_subdir/root_subdir/user_subdir)
    //   binds to
    //   @two_dir_user_url(/data/from/provider)
    //
    // @root_url: $root_dir(/data/to/user)
    //     -- $routed_from_root(root_subdir) -->
    //   @two_dir_user_url
    //     -- (user_subdir) --> (/data/from/root)
    //
    // I.e.,
    //   @root_url(/data/to/user/root_subdir/user_subdir)
    //   binds to
    //   @two_dir_user_url(/data/from/root)
    fn valid_two_node_two_dir_tree_model() -> Result<Arc<DataModel>> {
        let model = fake_data_model();
        let build_tree_result = ComponentTreeBuilder::new(hashmap! {
            "root_url".to_string() => ComponentDecl{
                capabilities: vec![
                    DirectoryDecl{
                        name: CapabilityName("root_dir".to_string()),
                        source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                        rights: fio2::Operations::Connect,
                    }.into(),
                ],
                offers: vec![
                    OfferDirectoryDecl{
                        source: OfferSource::static_child("one_dir_provider".to_string()),
                        source_name: CapabilityName("exposed_by_provider".to_string()),
                        target: OfferTarget::static_child("two_dir_user".to_string()),
                        target_name: CapabilityName("routed_from_provider".to_string()),
                        dependency_type: DependencyType::Strong,
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some(PathBuf::from_str("root_subdir").unwrap()),
                    }.into(),
                    OfferDirectoryDecl{
                        source: OfferSource::Self_,
                        source_name: CapabilityName("root_dir".to_string()),
                        target: OfferTarget::static_child("two_dir_user".to_string()),
                        target_name: CapabilityName("routed_from_root".to_string()),
                        dependency_type: DependencyType::Strong,
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some(PathBuf::from_str("root_subdir").unwrap()),
                    }.into(),
                ],
                children: vec![
                    ChildDecl{
                        name: "two_dir_user".to_string(),
                        url: "two_dir_user_url".to_string(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                    ChildDecl{
                        name: "one_dir_provider".to_string(),
                        url: "one_dir_provider_url".to_string(),
                        startup: fsys::StartupMode::Lazy,
                        on_terminate: None,
                        environment: None,
                    },
                ],
                ..ComponentDecl::default()
            },
            "two_dir_user_url".to_string() => ComponentDecl{
                uses: vec![
                    UseDirectoryDecl{
                        source: UseSource::Parent,
                        source_name: CapabilityName("routed_from_provider".to_string()),
                        target_path: CapabilityPath::from_str("/data/from/provider").unwrap(),
                        rights: fio2::Operations::Connect,
                        subdir: Some(PathBuf::from_str("user_subdir").unwrap()),
                        dependency_type: DependencyType::Strong,
                    }.into(),
                    UseDirectoryDecl{
                        source: UseSource::Parent,
                        source_name: CapabilityName("routed_from_root".to_string()),
                        target_path: CapabilityPath::from_str("/data/from/root").unwrap(),
                        rights: fio2::Operations::Connect,
                        subdir: Some(PathBuf::from_str("user_subdir").unwrap()),
                        dependency_type: DependencyType::Strong,
                    }.into(),
                ],
                ..ComponentDecl::default()
            },
            "one_dir_provider_url".to_string() => ComponentDecl{
                capabilities: vec![
                    DirectoryDecl{
                        name: CapabilityName("provider_dir".to_string()),
                        source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                        rights: fio2::Operations::Connect,
                    }.into(),
                ],
                exposes: vec![
                    ExposeDirectoryDecl{
                        source: ExposeSource::Self_,
                        source_name: CapabilityName("provider_dir".to_string()),
                        target: ExposeTarget::Parent,
                        target_name: CapabilityName("exposed_by_provider".to_string()),
                        rights: Some(fio2::Operations::Connect),
                        subdir: Some(PathBuf::from_str("provider_subdir").unwrap()),
                    }.into(),
                ],
                ..ComponentDecl::default()
            },
        })
        .build("root_url".to_string());
        let tree = build_tree_result.tree.ok_or(anyhow!("Failed to build component tree"))?;
        model.set(V2ComponentTree::new(tree, build_tree_result.errors))?;
        Ok(model)
    }

    #[test]
    fn test_component_routes_bad_request() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        assert_eq!(
            // Request JSON is invalid.
            controller.query(model, json!({"invalid": "request"})).err().unwrap().to_string(),
            BAD_REQUEST_CTX
        );

        Ok(())
    }

    #[test]
    fn test_component_routes_target_ok() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        ok_unwrap!(controller.query(
            model,
            // Vacuous request: Confirms that @root_url uses no input capabilities.
            json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec![]),
                    routes_to_skip: vec![],
                    routes_to_verify: vec![],
                },],
            })
        ));

        Ok(())
    }

    #[test]
    fn test_component_routes_missing_target() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let err = err_unwrap!(controller.query(
            model,
            // Request checking routes of a component instance that does not exist.
            json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec![
                        "does:0", "not:1", "exist:2"
                    ]),
                    routes_to_skip: vec![],
                    routes_to_verify: vec![],
                }],
            })
        ));
        // Not using `err_start_with` because matching `err` string, not
        // `err.root_cause()` string; `MISSING_TARGET_NODE` is the last context
        // attached to the error, which originates elsewhere.
        assert!(err.to_string().starts_with(MISSING_TARGET_NODE));

        Ok(())
    }

    #[test]
    fn test_route_lists_incomplete() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let err = err_unwrap!(controller.query(
            model,
            // Request fails because not all capabilities used by @two_dir_user_url are
            // listed in `routes_to_skip` + `routes_to_verify`.
            json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0",]),
                    routes_to_skip: vec![],
                    routes_to_verify: vec![],
                }],
            }),
        ));
        assert!(err.root_cause().to_string().starts_with(ROUTE_LISTS_INCOMPLETE));

        Ok(())
    }

    #[test]
    fn test_skip_all_routes() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let result = ok_unwrap!(controller.query(
            model,
            // Successful request that confirms but does not verify
            // sources on all capabilities used by @two_dir_user_url.
            json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0",]),
                    routes_to_skip: vec![
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                            name: None,
                        },
                    ],
                    routes_to_verify: vec![],
                }],
            }),
        ));
        assert_eq!(
            result,
            json!(hashmap! {
                "results" => hashmap!{
                    "/two_dir_user" => vec![] as Vec<VerifyComponentRouteResult<'_>>,
                },
            })
        );

        Ok(())
    }

    #[test]
    fn test_match_some_routes() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let request = RouteSourcesRequest {
            component_routes: vec![RouteSourcesSpec {
                target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0"]),
                routes_to_skip: vec![
                    // Skip @root_url -> @two_dir_user_url route.
                    UseSpec {
                        type_name: CapabilityTypeName::Directory,
                        path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                        name: None,
                    },
                ],
                routes_to_verify: vec![
                    // request.component_routes[0].routes_to_verify[0]:
                    // Match route:
                    //   @one_dir_provider_url
                    //     -> @root_url
                    //     -> @one_dir_user_url.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                // Match complete path with routed subdirs.
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/provider_subdir/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                ],
            }],
        };
        let result = ok_unwrap!(controller.query(model, json!(request)));

        assert_eq!(
            result,
            json!(hashmap! {
                "results" => hashmap!{
                    "/two_dir_user" => vec![
                        VerifyComponentRouteResult{
                            query: &request.component_routes[0].routes_to_verify[0],
                            result: Ok(Source {
                                node_path: request.component_routes[0].routes_to_verify[0].source.node_path.clone(),
                                capability: DirectoryDecl{
                                    name: CapabilityName("provider_dir".to_string()),
                                    source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                                    rights: fio2::Operations::Connect,
                                }.into(),
                            })
                        }
                    ],
                },
            })
        );

        Ok(())
    }

    #[test]
    fn test_match_some_routes_partial_path() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let request = RouteSourcesRequest {
            component_routes: vec![RouteSourcesSpec {
                target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0"]),
                routes_to_skip: vec![
                    // Skip @root_url -> @two_dir_user_url route.
                    UseSpec {
                        type_name: CapabilityTypeName::Directory,
                        path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                        name: None,
                    },
                ],
                routes_to_verify: vec![
                    // request.component_routes[0].routes_to_verify[0]:
                    // Match route:
                    //   @one_dir_provider_url
                    //     -> @root_url
                    //     -> @one_dir_user_url.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                // Match partial path with some (not all) routed
                                // subdirs.
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/provider_subdir/root_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                ],
            }],
        };
        let result = ok_unwrap!(controller.query(model, json!(request)));
        assert_eq!(
            result,
            json!(hashmap! {
                "results" => hashmap!{
                    "/two_dir_user" => vec![
                        VerifyComponentRouteResult{
                            query: &request.component_routes[0].routes_to_verify[0],
                            result: Ok(Source {
                                node_path: request.component_routes[0].routes_to_verify[0].source.node_path.clone(),
                                capability: DirectoryDecl{
                                    name: CapabilityName("provider_dir".to_string()),
                                    source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                                    rights: fio2::Operations::Connect,
                                }.into(),
                            })
                        }
                    ],
                },
            })
        );

        Ok(())
    }

    #[test]
    fn test_match_all_routes() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let request = RouteSourcesRequest {
            component_routes: vec![RouteSourcesSpec {
                target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0"]),
                routes_to_skip: vec![],
                routes_to_verify: vec![
                    // request.component_routes[0].routes_to_verify[0]:
                    // Match route: @root_url -> @two_dir_user_url route.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec![]),
                            capability: SourceDeclSpec {
                                // Match complete path with routed subdirs.
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("root_dir".to_string())),
                            },
                        },
                    },
                    // request.component_routes[0].routes_to_verify[1]:
                    // Match route:
                    //   @one_dir_provider_url
                    //     -> @root_url
                    //     -> @one_dir_user_url.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                // Match complete path with routed subdirs.
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/provider_subdir/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                ],
            }],
        };
        let result = ok_unwrap!(controller.query(model, json!(request)));
        assert_eq!(
            result,
            json!(hashmap! {
                "results" => hashmap!{
                    "/two_dir_user" => vec![
                        VerifyComponentRouteResult{
                            query: &request.component_routes[0].routes_to_verify[0],
                            result: Ok(Source {
                                node_path: request.component_routes[0].routes_to_verify[0].source.node_path.clone(),
                                capability: DirectoryDecl{
                                    name: CapabilityName("root_dir".to_string()),
                                    source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                                    rights: fio2::Operations::Connect,
                                }.into(),
                            })
                        },
                        VerifyComponentRouteResult{
                            query: &request.component_routes[0].routes_to_verify[1],
                            result: Ok(Source {
                                node_path: request.component_routes[0].routes_to_verify[1].source.node_path.clone(),
                                capability: DirectoryDecl{
                                    name: CapabilityName("provider_dir".to_string()),
                                    source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                                    rights: fio2::Operations::Connect,
                                }.into(),
                            })
                        }
                    ],
                },
            })
        );

        Ok(())
    }

    #[test]
    fn test_match_multiple_components() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let request =
            RouteSourcesRequest {
                component_routes: vec![
                    // Match empty set of routes used by @root_url.
                    RouteSourcesSpec {
                        target_node_path: NodePath::absolute_from_vec(vec![]),
                        routes_to_skip: vec![],
                        routes_to_verify: vec![],
                    },
                    // Match all routes used by @two_dir_user_url.
                    RouteSourcesSpec {
                        target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0"]),
                        routes_to_skip: vec![],
                        routes_to_verify: vec![
                    // request.component_routes[1].routes_to_verify[0]:
                    // Match route: @root_url -> @two_dir_user_url route.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec![]),
                            capability: SourceDeclSpec {
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("root_dir".to_string())),
                            },
                        },
                    },
                    // request.component_routes[1].routes_to_verify[1]:
                    // Match route:
                    //   @one_dir_provider_url
                    //     -> @root_url
                    //     -> @one_dir_user_url.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/provider_subdir/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                ],
                    },
                ],
            };
        let result = ok_unwrap!(controller.query(model, json!(request)));
        assert_eq!(
            result,
            json!(hashmap! {
                "results" => hashmap!{
                    "/" => vec![] as Vec<VerifyComponentRouteResult<'_>>,
                    "/two_dir_user" => vec![
                        VerifyComponentRouteResult{
                            query: &request.component_routes[1].routes_to_verify[0],
                            result: Ok(Source {
                                node_path: request.component_routes[1].routes_to_verify[0].source.node_path.clone(),
                                capability: DirectoryDecl{
                                    name: CapabilityName("root_dir".to_string()),
                                    source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                                    rights: fio2::Operations::Connect,
                                }.into(),
                            })
                        },
                        VerifyComponentRouteResult{
                            query: &request.component_routes[1].routes_to_verify[1],
                            result: Ok(Source {
                                node_path: request.component_routes[1].routes_to_verify[1].source.node_path.clone(),
                                capability: DirectoryDecl{
                                    name: CapabilityName("provider_dir".to_string()),
                                    source_path: Some(CapabilityPath::from_str("/data/to/user").unwrap()),
                                    rights: fio2::Operations::Connect,
                                }.into(),
                            })
                        }
                    ],
                },
            })
        );

        Ok(())
    }

    #[test]
    fn test_misconfigured_skip_match_source_name() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let source_name = "routed_from_provider";
        let controller = RouteSourcesController::default();
        let err = err_unwrap!(controller.query(
            model,
            json!(json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0",]),
                    routes_to_skip: vec![
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: None,
                            // This is the source name of the route, but
                            // directory uses are matched by target path.
                            name: Some(CapabilityName(source_name.to_string())),
                        },
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                            name: None,
                        },
                    ],
                    routes_to_verify: vec![],
                }],
            }))
        ));
        err_starts_with!(err, MATCH_ONE_FAILED);
        err_contains!(err, &format!("no {}", USE_SPEC_NAME));
        err_contains!(err, &format!("in {} list", USE_DECL_NAME));
        err_contains!(err, source_name);

        Ok(())
    }

    #[test]
    fn test_misconfigured_skip_match_source_name_and_target_path() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let target_path = "/data/from/provider";
        let source_name = "routed_from_provider";
        let controller = RouteSourcesController::default();
        let err = err_unwrap!(controller.query(
            model,
            json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0",]),
                    routes_to_skip: vec![
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            // This is the correct path, but also specifying
                            // source name should cause a failure.
                            path: Some(CapabilityPath::from_str(target_path).unwrap()),
                            // This is the source name of the route, but
                            // directory uses are matched by target path.
                            name: Some(CapabilityName(source_name.to_string())),
                        },
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                            name: None,
                        },
                    ],
                    routes_to_verify: vec![],
                }],
            }),
        ));
        err_starts_with!(err, MATCH_ONE_FAILED);
        err_contains!(err, &format!("no {}", USE_SPEC_NAME));
        err_contains!(err, &format!("in {} list", USE_DECL_NAME));
        err_contains!(err, target_path);
        err_contains!(err, source_name);

        Ok(())
    }

    #[test]
    fn test_skip_route_with_no_match() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let bad_path = "/does/not/exist";
        let controller = RouteSourcesController::default();
        let err = err_unwrap!(controller.query(
            model,
            // Skip correct routes, but also one that does not exist.
            json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0",]),
                    routes_to_skip: vec![
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                            name: None,
                        },
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str(bad_path).unwrap()),
                            name: None,
                        },
                    ],
                    routes_to_verify: vec![],
                }],
            }),
        ));
        err_starts_with!(err, MATCH_ONE_FAILED);
        err_contains!(err, &format!("no {}", USE_SPEC_NAME));
        err_contains!(err, &format!("in {} list", USE_DECL_NAME));
        err_contains!(err, bad_path);

        Ok(())
    }

    #[test]
    fn test_skip_duplicate() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let dup_name = "/data/from/root";
        let controller = RouteSourcesController::default();
        let err = err_unwrap!(controller.query(
            model,
            json!(RouteSourcesRequest {
                component_routes: vec![RouteSourcesSpec {
                    target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0",]),
                    routes_to_skip: vec![
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str(dup_name).unwrap()),
                            name: None,
                        },
                        // Intentional error: Duplicate match for same
                        // route-to-skip.
                        UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str(dup_name).unwrap()),
                            name: None,
                        },
                    ],
                    routes_to_verify: vec![],
                }],
            }),
        ));
        err_starts_with!(err, ROUTE_LISTS_OVERLAP);

        Ok(())
    }

    #[test]
    fn test_match_duplicate() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let controller = RouteSourcesController::default();
        let request = RouteSourcesRequest {
            component_routes: vec![RouteSourcesSpec {
                target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0"]),
                routes_to_skip: vec![UseSpec {
                    type_name: CapabilityTypeName::Directory,
                    path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                    name: None,
                }],
                routes_to_verify: vec![
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/provider").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/provider_subdir/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                    // Intentional error: Duplicate match for same
                    // route-to-verify.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                path_prefix: Some(
                                    CapabilityPath::from_str("/data/to/user").unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                ],
            }],
        };
        let err = err_unwrap!(controller.query(model, json!(request)));
        err_starts_with!(err, ROUTE_LISTS_OVERLAP);

        Ok(())
    }

    #[test]
    fn test_skip_match_duplicate() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let dup_name = "/data/from/provider";
        let controller = RouteSourcesController::default();
        let request = RouteSourcesRequest {
            component_routes: vec![RouteSourcesSpec {
                target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0"]),
                routes_to_skip: vec![
                    UseSpec {
                        type_name: CapabilityTypeName::Directory,
                        path: Some(CapabilityPath::from_str(dup_name).unwrap()),
                        name: None,
                    },
                    UseSpec {
                        type_name: CapabilityTypeName::Directory,
                        path: Some(CapabilityPath::from_str("/data/from/root").unwrap()),
                        name: None,
                    },
                ],
                routes_to_verify: vec![
                    // Intentional error: Route-to-verify match is duplicate of
                    // a route-to-skip match.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str(dup_name).unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/provider_subdir/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                ],
            }],
        };
        let err = err_unwrap!(controller.query(model, json!(request)));
        err_starts_with!(err, ROUTE_LISTS_OVERLAP);

        Ok(())
    }

    #[test]
    fn test_skip_match_duplicate_mixed() -> Result<()> {
        let model = valid_two_node_two_dir_tree_model()?;
        let dup_name = "/data/from/provider";
        let controller = RouteSourcesController::default();
        let request = RouteSourcesRequest {
            component_routes: vec![RouteSourcesSpec {
                target_node_path: NodePath::absolute_from_vec(vec!["two_dir_user:0"]),
                routes_to_skip: vec![
                    // Intentional error: No match for `/data/from/root`. That
                    // way number of routes to skip + number of routes to verify
                    // checks out, but route not every route is matched.
                    UseSpec {
                        type_name: CapabilityTypeName::Directory,
                        path: Some(CapabilityPath::from_str(dup_name).unwrap()),
                        name: None,
                    },
                ],
                routes_to_verify: vec![
                    // Intentional error: Route-to-verify match is duplicate of
                    // a route-to-skip match.
                    RouteMatch {
                        target: UseSpec {
                            type_name: CapabilityTypeName::Directory,
                            path: Some(CapabilityPath::from_str(dup_name).unwrap()),
                            name: None,
                        },
                        source: SourceSpec {
                            node_path: NodePath::absolute_from_vec(vec!["one_dir_provider:0"]),
                            capability: SourceDeclSpec {
                                path_prefix: Some(
                                    CapabilityPath::from_str(
                                        "/data/to/user/provider_subdir/root_subdir/user_subdir",
                                    )
                                    .unwrap(),
                                ),
                                name: Some(CapabilityName("provider_dir".to_string())),
                            },
                        },
                    },
                ],
            }],
        };
        let err = err_unwrap!(controller.query(model, json!(request)));
        err_contains!(err, ROUTE_LISTS_OVERLAP);
        err_contains!(err, ROUTE_LISTS_INCOMPLETE);

        Ok(())
    }
}
