// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::selectors, fidl_fuchsia_diagnostics::StringSelector, std::collections::HashSet};

/// Struct that encodes the information needed to
/// represent a component selector as a DFA. This DFA representation
/// is used to evaluate a component_hierarchy path against a selector
/// to determine if the data under the path is being selected for.
// TODO(fxbug.dev/47015): The component DFA is going to be unusued until we implement the custom
// DFA traversal for selection of specific properties on a hierarchy.
#[allow(dead_code)]
pub(crate) struct SelectorAutomata<'a> {
    // The individual states that make up
    // the selector state-machine.
    states: &'a Vec<StringSelector>,
    // The set of states that the automata is currently
    // validly at, based on its evaluatation of some input.
    state_indices: Option<HashSet<usize>>,
}

// TODO(fxbug.dev/47015): The component DFA is going to be unusued until we implement the custom
// DFA traversal for selection of specific properties on a hierarchy.
#[allow(dead_code)]
impl<'a> SelectorAutomata<'a> {
    pub fn new(states: &'a Vec<StringSelector>) -> Self {
        SelectorAutomata { states: states, state_indices: None }
    }

    /// Given a tokenized realmpath through the hub
    /// to a target component, evaluates a SelectorAutomata
    /// representing a component selector to see if the
    /// tokenized realm path is selected for by the automata.
    pub fn evaluate_automata_against_path(&mut self, hierarchy_path: &Vec<String>) -> bool {
        debug_assert!(&self.state_indices.is_none());

        // Initialize the DFA to be staged for execution starting
        // at the entry state. Note that we start at the end of the selector
        // and work backwards, since the most common usecase will have specified
        // target components that can be quickly filtered out.
        let entry_state_index = self.states.len() - 1;
        let mut curr_state_set = HashSet::new();
        curr_state_set.insert(entry_state_index);
        self.state_indices = Some(curr_state_set);

        for (path_index, path_value) in hierarchy_path.iter().enumerate().rev() {
            // We have to do extra state analysis on the final path value, to make
            // sure that there are not only stateful matches, but that atleast one
            // of those stateful matches is an exit state in the DFA.
            if path_index > 0 {
                let next_generation_states = match &self.state_indices {
                    Some(state_indices) => {
                        evaluate_single_generation(path_value, state_indices, self.states)
                    }
                    None => unreachable!(
                        "state indices should always be initialized at start of execution."
                    ),
                };
                if next_generation_states.is_empty() {
                    return false;
                } else {
                    self.state_indices = Some(next_generation_states);
                }
            } else {
                let final_generation_states = match &self.state_indices {
                    Some(state_indices) => state_indices,
                    None => unreachable!(
                        "even in the final generation, the state indices should not be None."
                    ),
                };

                // If we're at the final path value and not sitting at the exit
                // node, there's no way for us to match.
                if !final_generation_states.contains(&0) {
                    return false;
                }

                // If we're at the final path value and the exit node is present, the only
                // match result we care about is the result of the exit node.
                if !evaluate_path_state_with_selector_node(path_value, &self.states[0]) {
                    return false;
                }

                return true;
            }
        }
        unreachable!("There is no way to reach this point.");
    }
}

// Checks to see whether a given tokenized value
// in the hub path can be selected for by a given
// StringSelector.
fn evaluate_path_state_with_selector_node(
    path_state: &str,
    selector_node: &StringSelector,
) -> bool {
    match selector_node {
        // TODO(fxbug.dev/4601): String patterns that contain wildcards must be pattern matched on.
        //             Can we convert these to regex to avoid another custom state machine?
        StringSelector::StringPattern(string_pattern) => {
            if string_pattern == selectors::WILDCARD_SYMBOL_STR {
                return true;
            } else {
                // TODO(fxbug.dev/4601): Support wildcarded string_literals.
                return string_pattern == path_state;
            }
        }
        StringSelector::ExactMatch(exact_match) => exact_match == path_state,
        _ => false,
    }
}

// Evaluate a single generation of a given
// automata, and return the states of the next generation.
fn evaluate_single_generation(
    path_value: &String,
    state_indices: &HashSet<usize>,
    states: &Vec<StringSelector>,
) -> HashSet<usize> {
    let mut next_generation_state_indices: HashSet<usize> = HashSet::new();
    for state_index in state_indices {
        let state_node = &states[*state_index as usize];
        if evaluate_path_state_with_selector_node(&path_value, state_node) {
            // NOTE: If we reintroduce globs, at this point, glob states must
            // be re-added to the next_generation_state_indices since globs
            // can repeat.

            // Always provide the next state in the automata if the current
            // state was a match, assuming it's not the exit state that was
            // arrived at early.
            if *state_index > 0 {
                next_generation_state_indices.insert(state_index - 1);
            }
        }
    }
    next_generation_state_indices
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn canonical_automata_simulator_test() {
        let test_vector: Vec<(Vec<String>, Vec<StringSelector>, bool)> = vec![
            (
                vec!["a".to_string(), "b".to_string(), "c".to_string()],
                vec![
                    StringSelector::StringPattern("a".to_string()),
                    StringSelector::StringPattern("b".to_string()),
                    StringSelector::StringPattern("c".to_string()),
                ],
                true,
            ),
            (
                vec!["a".to_string(), "b".to_string(), "c".to_string()],
                vec![
                    StringSelector::StringPattern("*".to_string()),
                    StringSelector::StringPattern("b".to_string()),
                    StringSelector::StringPattern("c".to_string()),
                ],
                true,
            ),
            (
                vec!["a".to_string(), "b".to_string(), "c".to_string()],
                vec![
                    StringSelector::StringPattern("a".to_string()),
                    StringSelector::StringPattern("*".to_string()),
                    StringSelector::StringPattern("*".to_string()),
                ],
                true,
            ),
            (
                vec!["a".to_string(), "b".to_string(), "c".to_string()],
                vec![
                    StringSelector::StringPattern("b".to_string()),
                    StringSelector::StringPattern("*".to_string()),
                    StringSelector::StringPattern("*".to_string()),
                ],
                false,
            ),
            // TODO(fxbug.dev/4601): This should pass once wildcarded string literals
            //             are supported.
            (
                vec!["a".to_string(), "bob".to_string(), "c".to_string()],
                vec![
                    StringSelector::StringPattern("a".to_string()),
                    StringSelector::StringPattern("b*".to_string()),
                    StringSelector::StringPattern("c".to_string()),
                ],
                false,
            ),
        ];

        for (test_path, component_node_vec, result) in test_vector {
            let mut automata = SelectorAutomata::new(&component_node_vec);
            assert_eq!(result, automata.evaluate_automata_against_path(&test_path));
        }
    }
}
