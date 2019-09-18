// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use {
    failure::Fail,
    fidl_fuchsia_diagnostics::{ComponentSelector, PathSelectionNode, PatternMatcher},
    fidl_fuchsia_diagnostics_inspect::{PropertySelector, Selector, TreeSelector},
};
// Character used to delimit the different sections of an inspect selector,
// the component selector, the tree selector, and the property selector.
static SELECTOR_DELIMITER: char = ':';

// Character used to delimit nodes within a component hierarchy path.
static PATH_NODE_DELIMITER: char = '/';

// Character used to escape interperetation of this parser's "special
// characers"; *, /, :, and \.
static ESCAPE_CHARACTER: char = '\\';

// Pattern used to encode wildcard.
static WILDCARD_SYMBOL: &str = "*";

// Pattern used to encode globs.
static GLOB_SYMBOL: &str = "**";

#[derive(Fail, Debug)]
#[fail(display = "Selector parsing failed: {}", _0)]
pub struct SelectorParserError(String);

/// Parse a string into a FIDL PathSelectionNode structure.
fn convert_string_to_path_selection_node(string_to_convert: &String) -> PathSelectionNode {
    match string_to_convert {
        wildcard if wildcard == WILDCARD_SYMBOL => {
            PathSelectionNode::PatternMatcher(PatternMatcher::Wildcard)
        }
        glob if glob == GLOB_SYMBOL => PathSelectionNode::PatternMatcher(PatternMatcher::Glob),
        _ => PathSelectionNode::StringPattern(string_to_convert.to_string()),
    }
}

/// Parse a string into a FIDL PropertySelector structure.
///
/// Glob strings will throw SelectorParserErrors since globs are not
/// permitted for property selction.
fn convert_string_to_property_selector(
    string_to_convert: &String,
) -> Result<PropertySelector, SelectorParserError> {
    if string_to_convert.is_empty() {
        return Err(SelectorParserError(
            "Tree Selectors must have non-empty property selectors.".to_string(),
        ));
    }

    match string_to_convert {
        wildcard if wildcard == WILDCARD_SYMBOL => Ok(PropertySelector::Wildcard(true)),
        glob if glob == GLOB_SYMBOL => {
            Err(SelectorParserError("Property selectors don't support globs.".to_string()))
        }
        _ => Ok(PropertySelector::StringPattern(string_to_convert.to_string())),
    }
}

/// Increments the CharIndices iterator and updates the token builder
/// in order to avoid processing characters being escaped by the selector.
fn handle_escaped_char(
    token_builder: &mut String,
    selection_iter: &mut std::str::CharIndices,
) -> Result<(), SelectorParserError> {
    token_builder.push(ESCAPE_CHARACTER);
    let escaped_char_option: Option<(usize, char)> = selection_iter.next();
    match escaped_char_option {
        Some((_, escaped_char)) => token_builder.push(escaped_char),
        None => {
            return Err(SelectorParserError(
                "Selecter fails verification due to unmatched escape character".to_string(),
            ));
        }
    }
    Ok(())
}

/// Converts a string into a vector of string tokens representing the unparsed
/// string delimited by the provided delimiter, excluded escaped delimiters.
pub fn tokenize_string(
    untokenized_selector: &str,
    delimiter: char,
) -> Result<Vec<String>, SelectorParserError> {
    let mut token_aggregator = Vec::new();
    let mut curr_token_builder: String = String::new();
    let mut unparsed_selector_iter = untokenized_selector.char_indices();

    let empty_token_error_string = format!("Cannot have empty strings delimited by {}", delimiter);

    while let Some((_, selector_char)) = unparsed_selector_iter.next() {
        match selector_char {
            escape if escape == ESCAPE_CHARACTER => {
                handle_escaped_char(&mut curr_token_builder, &mut unparsed_selector_iter)?;
            }
            selector_delimiter if selector_delimiter == delimiter => {
                if curr_token_builder.is_empty() {
                    return Err(SelectorParserError(empty_token_error_string));
                }
                token_aggregator.push(curr_token_builder);
                curr_token_builder = String::new();
            }
            _ => curr_token_builder.push(selector_char),
        }
    }

    // Push the last section of the selector into the aggregator since we don't delimit the
    // end of the selector.
    if curr_token_builder.is_empty() {
        return Err(SelectorParserError(empty_token_error_string));
    }

    token_aggregator.push(curr_token_builder);
    return Ok(token_aggregator);
}

/// Converts an unparsed component selector string into a ComponentSelector.
pub fn parse_component_selector(
    unparsed_component_selector: &String,
) -> Result<ComponentSelector, SelectorParserError> {
    if unparsed_component_selector.is_empty() {
        return Err(SelectorParserError(
            "ComponentSelector must have atleast one path node.".to_string(),
        ));
    }

    let tokenized_component_selector =
        tokenize_string(unparsed_component_selector, PATH_NODE_DELIMITER)?;

    let mut component_selector: ComponentSelector = ComponentSelector::empty();

    // Convert every token of the component hierarchy into a PathSelectionNode.
    let mut path_node_vector = tokenized_component_selector
        .iter()
        .map(|node_string| convert_string_to_path_selection_node(node_string))
        .collect::<Vec<PathSelectionNode>>();

    // The last entry in the tokenized vector is the target_component, so process
    // and assign it to the ComponentSelector first.
    component_selector.target_component_node = Some(path_node_vector.pop().unwrap());

    match path_node_vector.as_slice() {
        [] => component_selector.component_hierarchy_path = None,
        _ => component_selector.component_hierarchy_path = Some(path_node_vector),
    }

    return Ok(component_selector);
}

/// Converts an unparsed node path selector and an unparsed property selector into
/// a TreeSelector.
pub fn parse_tree_selector(
    unparsed_node_path: &String,
    unparsed_property_selector: &String,
) -> Result<TreeSelector, SelectorParserError> {
    let mut tree_selector: TreeSelector = TreeSelector::empty();

    if unparsed_node_path.is_empty() {
        tree_selector.node_path = None;
    } else {
        tree_selector.node_path = Some(
            tokenize_string(unparsed_node_path, PATH_NODE_DELIMITER)?
                .iter()
                .map(|node_string| convert_string_to_path_selection_node(node_string))
                .collect::<Vec<PathSelectionNode>>(),
        );
    }

    tree_selector.target_properties =
        Some(convert_string_to_property_selector(unparsed_property_selector)?);

    return Ok(tree_selector);
}

/// Converts an unparsed Inspect selector into a ComponentSelector and TreeSelector.
pub fn parse_selector(unparsed_selector: &str) -> Result<Selector, SelectorParserError> {
    // Tokenize the selector by `:` char in order to process each subselector separately.
    let selector_sections = tokenize_string(unparsed_selector, SELECTOR_DELIMITER)?;

    match selector_sections.as_slice() {
        [component_selector, inspect_node_selector, property_selector] => Ok(Selector {
            component_selector: parse_component_selector(component_selector)?,
            tree_selector: parse_tree_selector(inspect_node_selector, property_selector)?,
        }),
        _ => Err(SelectorParserError(
            "Selector format requires exactly 3 subselectors delimited by a `:`.".to_string(),
        )),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn canonical_component_selector_test() {
        let test_vector: Vec<(
            String,
            PathSelectionNode,
            PathSelectionNode,
            Option<PathSelectionNode>,
        )> = vec![
            (
                "a/b/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern("b".to_string()),
                Some(PathSelectionNode::StringPattern("c".to_string())),
            ),
            (
                "a/*/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::PatternMatcher(PatternMatcher::Wildcard),
                Some(PathSelectionNode::StringPattern("c".to_string())),
            ),
            (
                "a/**/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::PatternMatcher(PatternMatcher::Glob),
                Some(PathSelectionNode::StringPattern("c".to_string())),
            ),
            (
                "a/b*/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern("b*".to_string()),
                Some(PathSelectionNode::StringPattern("c".to_string())),
            ),
            (
                r#"a/b\*/c"#.to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern(r#"b\*"#.to_string()),
                Some(PathSelectionNode::StringPattern("c".to_string())),
            ),
            (
                r#"a/\*/c"#.to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern(r#"\*"#.to_string()),
                Some(PathSelectionNode::StringPattern("c".to_string())),
            ),
        ];

        for (test_string, first_path_node, second_path_node, target_component) in test_vector {
            let component_selector = parse_component_selector(&test_string).unwrap();
            assert_eq!(component_selector.target_component_node, target_component);
            match component_selector.component_hierarchy_path.as_ref().unwrap().as_slice() {
                [first, second] => {
                    assert_eq!(*first, first_path_node);
                    assert_eq!(*second, second_path_node);
                }
                _ => unreachable!(),
            }
        }
    }

    #[test]
    fn missing_path_component_selector_test() {
        let component_selector_string = "c";
        let component_selector =
            parse_component_selector(&component_selector_string.to_string()).unwrap();
        assert_eq!(
            component_selector.target_component_node,
            Some(PathSelectionNode::StringPattern("c".to_string()))
        );

        assert_eq!(component_selector.component_hierarchy_path, None);
    }

    #[test]
    fn path_components_have_spaces_as_names_selector_test() {
        let component_selector_string = " ";
        let component_selector =
            parse_component_selector(&component_selector_string.to_string()).unwrap();
        assert_eq!(
            component_selector.target_component_node,
            Some(PathSelectionNode::StringPattern(" ".to_string()))
        );

        assert_eq!(component_selector.component_hierarchy_path, None);
    }

    #[test]
    fn errorful_component_selector_test() {
        let test_vector: Vec<String> = vec!["".to_string(), "a\\".to_string()];
        for test_string in test_vector {
            let component_selector_result = parse_component_selector(&test_string);
            assert!(component_selector_result.is_err());
        }
    }

    #[test]
    fn canonical_tree_selector_test() {
        let test_vector: Vec<(
            String,
            String,
            PathSelectionNode,
            PathSelectionNode,
            Option<PropertySelector>,
        )> = vec![
            (
                "a/b".to_string(),
                "c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern("b".to_string()),
                Some(PropertySelector::StringPattern("c".to_string())),
            ),
            (
                "a/*".to_string(),
                "c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::PatternMatcher(PatternMatcher::Wildcard),
                Some(PropertySelector::StringPattern("c".to_string())),
            ),
            (
                "a/b".to_string(),
                "*".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern("b".to_string()),
                Some(PropertySelector::Wildcard(true)),
            ),
            (
                "a/**".to_string(),
                "c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::PatternMatcher(PatternMatcher::Glob),
                Some(PropertySelector::StringPattern("c".to_string())),
            ),
        ];

        for (
            test_node_path,
            test_target_property,
            first_path_node,
            second_path_node,
            parsed_property,
        ) in test_vector
        {
            let tree_selector =
                parse_tree_selector(&test_node_path, &test_target_property).unwrap();
            assert_eq!(tree_selector.target_properties, parsed_property);
            match tree_selector.node_path.as_ref().unwrap().as_slice() {
                [first, second] => {
                    assert_eq!(*first, first_path_node);
                    assert_eq!(*second, second_path_node);
                }
                _ => unreachable!(),
            }
        }
    }

    #[test]
    fn errorful_tree_selector_test() {
        let test_vector: Vec<(String, String)> = vec![
            // Not allowed due to empty property selector.
            ("a/b".to_string(), "".to_string()),
            // Not allowed due to glob property selector.
            ("a/b".to_string(), "**".to_string()),
            // Not allowed due to escape-char without a thing to escape.
            (r#"a/b\"#.to_string(), "c".to_string()),
        ];
        for (test_nodepath, test_targetproperty) in test_vector {
            let tree_selector_result = parse_tree_selector(&test_nodepath, &test_targetproperty);
            assert!(tree_selector_result.is_err());
        }
    }

    #[test]
    fn missing_nodepath_tree_selector_test() {
        let tree_selector_result = parse_tree_selector(&"".to_string(), &"c".to_string()).unwrap();
        assert_eq!(tree_selector_result.node_path, None);
        assert_eq!(
            tree_selector_result.target_properties,
            Some(PropertySelector::StringPattern("c".to_string()))
        );
    }
}
