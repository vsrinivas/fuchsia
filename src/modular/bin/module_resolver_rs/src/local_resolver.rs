// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_modular::{
        FindModulesParameterConstraint, FindModulesQuery, FindModulesResponse, FindModulesResult,
        FindModulesStatus,
    },
    std::collections::HashMap,
};

/// An `Action` describes a particular action to handle (e.g. com.fuchsia.navigate).
type Action = String;

/// An `ComponentUrl` is a Component URL to a module.
type ComponentUrl = String;

/// A `ParameterName` describes the name of an Action's parameter.
type ParameterName = String;

/// A `ParameterType` describes the type of an Action's parameter.
type ParameterType = String;

/// A `ModuleActionIndex` is a map from every supported `Action` to the set of modules which can
/// handle it.
pub type ModuleActionIndex = HashMap<Action, Vec<ModuleFacet>>;

#[derive(Clone, Debug)]
// A module facet describes the intent filters supported by a particular module.
pub struct ModuleFacet {
    // The Component URL of module.
    pub component_url: ComponentUrl,

    // The intent filters for all the actions supported by the module.
    pub intent_filters: Option<Vec<IntentFilter>>,
}

#[derive(Clone, Debug)]
// An intent filter describes an action, its parameters, and the parameter types.
pub struct IntentFilter {
    // The action this intent filter describes.
    pub action: Action,

    // The parameters associated with `action`.
    pub parameters: HashMap<ParameterName, ParameterType>,
}

/// Finds all the modules which match the provided `query`.
///
/// In order to satisfy a query a module must declare that it:
///   - Has an intent filter which matches `query.action`.
///   - The intent filter for `query.action` satisfies the `query.parameters`.
///
/// If a `query.handler` is set, `find_modules` checks whether the above conditions are satisfied
/// by the specified handler.
///
/// Parameters:
/// - `query`: The query to handle.
/// - `index`: The index to use for the search. If `query.handler` is set, the index is not
///            consulted.
///
/// Returns:
/// A vector of `ModuleFacet`s for the modules which satisfy `query`.
pub fn find_modules(query: &FindModulesQuery, index: &ModuleActionIndex) -> FindModulesResponse {
    let handler: Option<ComponentUrl> = query.handler.clone();
    let action: &Action = &query.action;
    let parameters: &Vec<FindModulesParameterConstraint> = &query.parameter_constraints;

    // The candidates are all facets which contain an intent filter for `action`, and match `handler`
    // if a handler is set.
    let candidates = index.get(action).into_iter().flatten().filter(|facet| {
        handler.as_ref().map_or(true, |unwrapped_handler| &facet.component_url == unwrapped_handler)
    });

    // For each of the candidates, determine whether or not the candidate can handle the specified
    // action with the given parameters.
    let matching_candidates =
        candidates.filter(|candidate| does_module_match_query(candidate, action, &parameters));

    // Convert the matching candidates to `FindModulesResult`s.
    let results = matching_candidates
        .map(|facet| FindModulesResult { module_id: facet.component_url.clone(), manifest: None })
        .collect();

    FindModulesResponse { status: FindModulesStatus::Success, results }
}

/// Checks whether or not the given module supports an action which satisfies the query.
///
/// Parameters:
/// - `module`: The `ModuleFacet` desribing the module which is being checked.
/// - `query_action`: The action of the query.
/// - `query_parameters`: The parameters associated with the `query_action`.
///
/// Returns:
/// `true` iff the `module_facet` contains an intent filter matching the query.
fn does_module_match_query(
    module_facet: &ModuleFacet,
    query_action: &Action,
    query_parameters: &[FindModulesParameterConstraint],
) -> bool {
    if let Some(intent_filters) = &module_facet.intent_filters {
        return intent_filters
            .iter()
            .find(|intent_filter| {
                does_intent_filter_match_query(*intent_filter, query_action, query_parameters)
            })
            .is_some();
    }

    false
}

/// Checks whether or not a given intent filter matches a query.
///
/// Parameters:
/// - `intent_filter`: The intent filter to check.
/// - `query_action`: The action of the query.
/// - `query_parameters`: The parameters associated with the `query_action`.
///
/// Returns:
/// `true` iff the `intent_filter` matches the query.
fn does_intent_filter_match_query(
    IntentFilter { action: filter_action, parameters: filter_parameters }: &IntentFilter,
    query_action: &Action,
    query_parameters: &[FindModulesParameterConstraint],
) -> bool {
    if filter_action != query_action {
        // The action names did not match.
        return false;
    }

    if filter_parameters.len() != query_parameters.len() {
        // The intent filter did not have the appropriate number of parameters.
        return false;
    }

    for FindModulesParameterConstraint {
        param_name: query_parameter_name,
        param_types: query_parameter_types,
    } in query_parameters
    {
        if let Some(filter_parameter_type) = filter_parameters.get(query_parameter_name) {
            if query_parameter_types
                .iter()
                .find(|query_parameter_type| *query_parameter_type == filter_parameter_type)
                .is_none()
            {
                // The intent filter's parameter type did not match any of the queries supported types.
                return false;
            }
        } else {
            // The parameter was not found in the intent filter.
            return false;
        }
    }

    true
}

#[cfg(test)]
mod tests {
    use super::find_modules;
    use super::Action;
    use super::ComponentUrl;
    use super::IntentFilter;
    use super::ModuleActionIndex;
    use super::ModuleFacet;

    use fidl_fuchsia_modular::FindModulesParameterConstraint;
    use fidl_fuchsia_modular::FindModulesQuery;
    use fidl_fuchsia_modular::FindModulesResponse;
    use maplit::hashmap;
    use std::collections::HashMap;

    /// Creates a `ModuleActionIndex` from the provided `ModuleFacet`s.
    ///
    /// Parameters:
    /// - `facets`: The facets which will be used to create the index.
    ///
    /// Returns:
    /// An index for all the actions supported by `facets`.
    fn index_facets(facets: &[ModuleFacet]) -> ModuleActionIndex {
        let mut index = ModuleActionIndex::new();
        for facet in facets {
            if let Some(intent_filters) = &facet.intent_filters {
                for intent_filter in intent_filters {
                    index
                        .entry(intent_filter.action.clone())
                        .or_insert(Vec::new())
                        .push(facet.clone());
                }
            }
        }

        index
    }

    /// Executes a `find_modules` query with the given action and parameters, against the given module index.
    ///
    /// Parameters:
    /// - `query_handler`: The handler to use for the query.
    /// - `query_action`: The action to use for the query.
    /// - `query_parameters`: The parameter constraints for the action to use for the query.
    /// - `module_index`: The modules and their intent filters to perform the query against.
    ///
    /// Returns:
    /// The result of executing the constructed `find_modules` query against the specified `module_index`.
    fn execute_query(
        query_handler: Option<ComponentUrl>,
        query_action: Action,
        query_parameters: Vec<FindModulesParameterConstraint>,
        module_index: HashMap<ComponentUrl, Vec<IntentFilter>>,
    ) -> FindModulesResponse {
        let facets: Vec<ModuleFacet> = module_index
            .into_iter()
            .map(|(handler, intent_filters)| ModuleFacet {
                component_url: handler,
                intent_filters: Some(intent_filters),
            })
            .collect();
        let index = index_facets(&facets);

        let query = FindModulesQuery {
            handler: query_handler,
            action: query_action.clone(),
            parameter_constraints: query_parameters,
        };

        find_modules(&query, &index)
    }

    /// Runs one or more test case descriptions against a given module index.
    ///
    ///
    /// Parameters:
    /// - `index HashMap<ComponentUrl, Vec<IntentFilter>`: The module index to query against.
    /// - `test_name`: The name of the test.
    ///   * `query_handler: Option<ComponentUrl>`: The handler for the query.
    ///   * `query_action: String`: The action name for the query.
    ///   * `query_parameters: HashMap<String, String>`: The parameters for the query.
    ///   * `num_results: Int`: The number of expected results when executing the query.
    ///
    /// Example:
    /// test_queries_against_index! {
    ///   index => hashmap! { ... }
    ///   first_test_case => {
    ///     ...
    ///   }
    ///   second_test_case => {
    ///     ...
    ///   }
    /// }
    macro_rules! test_queries_against_index {
        (
            index: $index:expr,
            $(
                $test_name:ident => {
                    query_handler = $query_handler:expr,
                    query_action = $query_action:expr,
                    query_parameters = $query_parameters:expr,
                    num_results = $num_results:expr,
                }
            )+
        ) =>
        {
            $(
                #[test]
                fn $test_name() {
                    assert_eq!(
                        execute_query($query_handler, $query_action, $query_parameters, $index)
                            .results.len(),
                        $num_results);
                }
            )+
        }
    }

    // Tests which are run against an index with a single module handling a single action with no
    // parameters.
    test_queries_against_index! {
        index: hashmap!{
            "test_module".to_string() => vec![
                IntentFilter{ action: "action".to_string(), parameters: hashmap!{}}
            ]
        },
        // Search with a query with no handler and an indexed action.
        match_action_no_parameters => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![],
            num_results = 1,
        }
        // Search with a query which has an action which is not indexed.
        mismatch_action_no_parameters => {
            query_handler = None,
            query_action = "different_action".to_string(),
            query_parameters = vec![],
            num_results = 0,
        }
        // Search with a handler which matches the indexed module.
        match_handler => {
            query_handler = Some("test_module".to_string()),
            query_action = "action".to_string(),
            query_parameters = vec![],
            num_results = 1,
        }
        // Search with a handler which does not match the indexed module, but does match the action.
        mismatch_handler => {
            query_handler = Some("not_test_module".to_string()),
            query_action = "action".to_string(),
            query_parameters = vec![],
            num_results = 0,
        }
    }

    // Tests which are run against an index containing one module, which handles one action with
    // one parameter.
    test_queries_against_index! {
        index: hashmap!{
            "test_module".to_string() => vec![
                IntentFilter{
                    action: "action".to_string(),
                    parameters: hashmap!{
                        "parameter_name".to_string() => "parameter_type".to_string()
                    }
                }
            ]
        },
        // Search with a matching action an parameter.
        match_action_and_parameters => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![ FindModulesParameterConstraint {
                param_name: "parameter_name".to_string(),
                param_types: vec!["parameter_type".to_string()],
            }],
            num_results = 1,
        }
        // Search with a matchin action and parameter name, but incorrect parameter type.
        mismatch_parameter_type => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![ FindModulesParameterConstraint {
                param_name: "parameter_name".to_string(),
                param_types: vec!["not_parameter_type".to_string()],
            }],
            num_results = 0,
        }
        // Search with a matchin action and parameter type, but incorrect parameter name.
        mismatch_parameter_name => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![ FindModulesParameterConstraint {
                param_name: "not_parameter_name".to_string(),
                param_types: vec!["parameter_type".to_string()],
            }],
            num_results = 0,
        }
        // Search with an incorrect action but matching parameters.
        mismatch_action_match_parameters => {
            query_handler = None,
            query_action = "not_action".to_string(),
            query_parameters = vec![ FindModulesParameterConstraint {
                param_name: "parameter_name".to_string(),
                param_types: vec!["parameter_type".to_string()],
            }],
            num_results = 0,
        }
        // Search with a query which has more parameters than the indexed action.
        query_has_more_parameters => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![
                FindModulesParameterConstraint {
                    param_name: "parameter_name".to_string(),
                    param_types: vec!["parameter_type".to_string()],
                },
                FindModulesParameterConstraint {
                    param_name: "second_parameter_name".to_string(),
                    param_types: vec!["parameter_type".to_string()],
                }
            ],
            num_results = 0,
        }
        // Search with a query which has no parameters but the same action name.
        query_has_no_parameters => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![
            ],
            num_results = 0,
        }
    }

    // Tests which are run against an index containing one module, which handles one action with
    // two parameters.
    test_queries_against_index! {
        // The index contains one action which takes two parameters.
        index: hashmap!{
            "test_module".to_string() => vec![
                IntentFilter{
                    action: "action".to_string(),
                    parameters: hashmap!{
                        "parameter_name".to_string() => "parameter_type".to_string(),
                        "second_parameter_name".to_string() => "second_parameter_type".to_string()
                    }
                }
            ]
        },
        // Search with a query which has one matching parameter but not both.
        query_has_fewer_parameters => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![
                FindModulesParameterConstraint {
                    param_name: "parameter_name".to_string(),
                    param_types: vec!["parameter_type".to_string()],
                }
            ],
            num_results = 0,
        }
    }

    // Tests which are run against an index containing two modules which both support the same
    // action. One of the modules also supports an additional action.
    test_queries_against_index! {
        index: hashmap!{
            "test_module".to_string() => vec![
                IntentFilter{
                    action: "action".to_string(),
                    parameters: hashmap!{
                        "parameter_name".to_string() => "parameter_type".to_string(),
                    }
                }
            ],
            "test_module_two".to_string() => vec![
                IntentFilter{
                    action: "action".to_string(),
                    parameters: hashmap!{
                        "parameter_name".to_string() => "parameter_type".to_string(),
                    }
                },
                IntentFilter{
                    action: "second_action".to_string(),
                    parameters: hashmap!{
                        "parameter_name".to_string() => "parameter_type".to_string(),
                        "second_parameter_name".to_string() => "second_parameter_type".to_string()
                    }
                }

            ]
        },
        // Search with a query which matches multiple modules.
        multiple_matches => {
            query_handler = None,
            query_action = "action".to_string(),
            query_parameters = vec![
                FindModulesParameterConstraint {
                    param_name: "parameter_name".to_string(),
                    param_types: vec!["parameter_type".to_string()],
                }
            ],
            num_results = 2,
        }
        // Search with a query which matches only one of the indexed modules.
        multiple_candidates_single_match => {
            query_handler = None,
            query_action = "second_action".to_string(),
            query_parameters = vec![
                FindModulesParameterConstraint {
                    param_name: "parameter_name".to_string(),
                    param_types: vec!["parameter_type".to_string()],
                },
                FindModulesParameterConstraint {
                    param_name: "second_parameter_name".to_string(),
                    param_types: vec!["second_parameter_type".to_string()],
                }
            ],
            num_results = 1,
        }
    }
}
