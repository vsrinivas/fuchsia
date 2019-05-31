// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

/// Matching Actions
///
//// Matches parameters types to a list of input types (with duplicates)
//// and matches a text query against the text in the Action.
use crate::models::Action;
use std::cmp::min;

/// true if elements in a are covered by elements in b.
///
/// This is a list implementation of a multiset.
///
/// Assumes a and b are sorted.
///
/// [1, 3] is covered by [1, 2, 3]
/// [1, 1] is covered by [1, 1, 2, 3]
/// [1, 1] is not covered by [1, 2, 3]
//
fn covers(a: &Vec<&str>, b: &Vec<&str>) -> bool {
    // return false when any element in a is not in b
    let mut it = b.into_iter();
    for a_val in a {
        loop {
            match it.next() {
                Some(x) if x == a_val => break, // step to next a_val
                Some(_) => continue,            // keep looking
                None => return false,           // not found in types
            }
        }
    }
    true
}

/// Match the query against text.
///
/// returns true if
///  - query is a case insensitive prefix of any word in text
///  - query or text is empty.
///
fn query_text_match(query: &str, text: &str) -> bool {
    query.is_empty()
        || text.is_empty()
        || text
            .split_whitespace()
            .any(|w| w[0..min(query.len(), w.len())].eq_ignore_ascii_case(query))
}

/// Match the query against text in the Action
///
/// returns true if query string matches action keywords
///
// TODO: match against keywords rather than display_info.title
//
fn query_action_match(action: &Action, query: &str) -> bool {
    match &action.display_info.display_info {
        Some(display_info) => match &display_info.title {
            Some(title) => query_text_match(query, title),
            None => false,
        },
        None => false,
    }
}

/// Match a vector of actions.
///
/// types must be sorted
///
/// returns a vector of the indices of all matching actions
///
pub fn action_match(query: &str, actions: &Vec<Action>, types: &Vec<&str>) -> Vec<usize> {
    // from_iter(types) results in double borrow &&str error, so add dereference
    let mut output = vec![];
    for index in 0..actions.len() {
        let mut params: Vec<&str> =
            actions[index].parameters.iter().map(|p| p.parameter_type.as_str()).collect();
        params.sort();
        if covers(&params, &types) && query_action_match(&actions[index], query) {
            output.push(index);
        }
    }
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    // Test the query_text_match function
    fn test_query_text_match() {
        let text = "Weather in $place";
        assert!(query_text_match("Weather", text), "First word didn't match");
        assert!(query_text_match("weather", text), "Lowercase word failed");
        assert!(query_text_match("Wea", text), "Prefix failed");
        assert!(!query_text_match(text, "Wea"), "Long query failed");
        assert!(query_text_match("In", text), "Middle word failed");
        assert!(query_text_match("", text), "Empty query failed");
        assert!(query_text_match("Weather", ""), "Empty text failed");
    }

    #[test]
    // Test the action_match function
    fn test_action_match() {
        let actions: Vec<Action> =
            serde_json::from_str(include_str!("../test_data/test_actions.json")).unwrap();

        let types = vec!["https://schema.org/Place"];
        let matches = action_match("weather", &actions, &types);
        assert_eq!(matches.len(), 1);
        assert_eq!(matches[0], 1);

        let matches = action_match("neither", &actions, &types);
        assert_eq!(matches.len(), 0);

        let matches = action_match("Listen", &actions, &types);
        assert_eq!(matches.len(), 0);

        let types = vec!["https://schema.org/MusicGroup"];
        let matches = action_match("Listen", &actions, &types);
        assert_eq!(matches.len(), 1);
        assert_eq!(matches[0], 0);
    }

    #[test]
    // Test the covers function
    fn test_covers() {
        let mut types = vec![
            "https://schema.org/MusicGroup",
            "https://schema.org/Place",
            "https://schema.org/Place",
        ];
        types.sort();

        let params = vec!["https://schema.org/MusicGroup"];
        assert!(covers(&params, &types), "[MusicGroup] is not covered");

        let params = vec!["https://schema.org/Song"];
        assert!(!covers(&params, &types), "[Song] is covered");

        let params = vec!["https://schema.org/Place"];
        assert!(covers(&params, &types), "[Place] is not covered");

        let params = vec!["https://schema.org/Place", "https://schema.org/Place"];
        assert!(covers(&params, &types), "[Place, Place] is not covered");

        let params = vec![
            "https://schema.org/Place",
            "https://schema.org/Place",
            "https://schema.org/Place",
        ];
        assert!(!covers(&params, &types), "[Place, Place, Place] is covered");
    }
}
