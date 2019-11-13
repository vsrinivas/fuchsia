// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
use {
    failure::{format_err, Error},
    fidl_fuchsia_diagnostics::{ComponentSelector, PathSelectionNode, PatternMatcher},
    fidl_fuchsia_diagnostics_inspect::{PropertySelector, Selector, TreeSelector},
    regex::Regex,
    std::fs,
    std::io::{BufRead, BufReader},
    std::path::{Path, PathBuf},
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

// Globs will match everything along a moniker, but won't match empty strings.
static GLOB_REGEX_EQUIVALENT: &str = ".+";

// Wildcards will match anything except for an unescaped slash, since their match
// only extends to a single moniker "node".
static WILDCARD_REGEX_EQUIVALENT: &str = r#"(\\/|[^/])+"#;

/// Parse a string into a FIDL PathSelectionNode structure.
fn convert_string_to_path_selection_node(
    string_to_convert: &String,
) -> Result<PathSelectionNode, Error> {
    match string_to_convert {
        wildcard if wildcard == WILDCARD_SYMBOL => {
            Ok(PathSelectionNode::PatternMatcher(PatternMatcher::Wildcard))
        }
        glob if glob == GLOB_SYMBOL => Ok(PathSelectionNode::PatternMatcher(PatternMatcher::Glob)),
        _ => {
            if string_to_convert.contains(GLOB_SYMBOL) {
                Err(format_err!("String literals can't contain globs."))
            } else {
                Ok(PathSelectionNode::StringPattern(string_to_convert.to_string()))
            }
        }
    }
}

/// Parse a string into a FIDL PropertySelector structure.
///
/// Glob strings will throw Errorsince globs are not
/// permitted for property selection.
fn convert_string_to_property_selector(
    string_to_convert: &String,
) -> Result<PropertySelector, Error> {
    if string_to_convert.is_empty() {
        return Err(format_err!("Tree Selectors must have non-empty property selectors.",));
    }

    match string_to_convert {
        wildcard if wildcard == WILDCARD_SYMBOL => Ok(PropertySelector::Wildcard(true)),
        glob if glob == GLOB_SYMBOL => Err(format_err!("Property selectors don't support globs.")),
        _ => {
            if string_to_convert.contains(GLOB_SYMBOL) {
                Err(format_err!("String literals can't contain globs."))
            } else {
                Ok(PropertySelector::StringPattern(string_to_convert.to_string()))
            }
        }
    }
}

/// Increments the CharIndices iterator and updates the token builder
/// in order to avoid processing characters being escaped by the selector.
fn handle_escaped_char(
    token_builder: &mut String,
    selection_iter: &mut std::str::CharIndices,
) -> Result<(), Error> {
    token_builder.push(ESCAPE_CHARACTER);
    let escaped_char_option: Option<(usize, char)> = selection_iter.next();
    match escaped_char_option {
        Some((_, escaped_char)) => token_builder.push(escaped_char),
        None => {
            return Err(format_err!(
                "Selecter fails verification due to unmatched escape character",
            ));
        }
    }
    Ok(())
}

/// Converts a string into a vector of string tokens representing the unparsed
/// string delimited by the provided delimiter, excluded escaped delimiters.
pub fn tokenize_string(untokenized_selector: &str, delimiter: char) -> Result<Vec<String>, Error> {
    let mut token_aggregator = Vec::new();
    let mut curr_token_builder: String = String::new();
    let mut unparsed_selector_iter = untokenized_selector.char_indices();

    while let Some((_, selector_char)) = unparsed_selector_iter.next() {
        match selector_char {
            escape if escape == ESCAPE_CHARACTER => {
                handle_escaped_char(&mut curr_token_builder, &mut unparsed_selector_iter)?;
            }
            selector_delimiter if selector_delimiter == delimiter => {
                if curr_token_builder.is_empty() {
                    return Err(format_err!(
                        "Cannot have empty strings delimited by {}",
                        delimiter
                    ));
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
        return Err(format_err!("Cannot have empty strings delimited by {}", delimiter));
    }

    token_aggregator.push(curr_token_builder);
    return Ok(token_aggregator);
}

/// Converts an unparsed component selector string into a ComponentSelector.
pub fn parse_component_selector(
    unparsed_component_selector: &String,
) -> Result<ComponentSelector, Error> {
    if unparsed_component_selector.is_empty() {
        return Err(format_err!("ComponentSelector must have atleast one path node.",));
    }

    let tokenized_component_selector =
        tokenize_string(unparsed_component_selector, PATH_NODE_DELIMITER)?;

    let mut component_selector: ComponentSelector = ComponentSelector::empty();

    // Convert every token of the component hierarchy into a PathSelectionNode.
    let path_node_vector = tokenized_component_selector
        .iter()
        .map(|node_string| convert_string_to_path_selection_node(node_string))
        .collect::<Result<Vec<PathSelectionNode>, Error>>()?;

    match path_node_vector.as_slice() {
        [] => {
            return Err(format_err!("ComponentSelector must have atleast one path node.",));
        }
        _ => component_selector.component_moniker = Some(path_node_vector),
    }

    return Ok(component_selector);
}

/// Converts an unparsed node path selector and an unparsed property selector into
/// a TreeSelector.
pub fn parse_tree_selector(
    unparsed_node_path: &String,
    unparsed_property_selector: &String,
) -> Result<TreeSelector, Error> {
    let mut tree_selector: TreeSelector = TreeSelector::empty();

    if unparsed_node_path.is_empty() {
        tree_selector.node_path = None;
    } else {
        tree_selector.node_path = Some(
            tokenize_string(unparsed_node_path, PATH_NODE_DELIMITER)?
                .iter()
                .map(|node_string| convert_string_to_path_selection_node(node_string))
                .collect::<Result<Vec<PathSelectionNode>, Error>>()?,
        );
    }

    tree_selector.target_properties =
        Some(convert_string_to_property_selector(unparsed_property_selector)?);

    return Ok(tree_selector);
}

/// Converts an unparsed Inspect selector into a ComponentSelector and TreeSelector.
pub fn parse_selector(unparsed_selector: &str) -> Result<Selector, Error> {
    // Tokenize the selector by `:` char in order to process each subselector separately.
    let selector_sections = tokenize_string(unparsed_selector, SELECTOR_DELIMITER)?;

    match selector_sections.as_slice() {
        [component_selector, inspect_node_selector, property_selector] => Ok(Selector {
            component_selector: parse_component_selector(component_selector)?,
            tree_selector: parse_tree_selector(inspect_node_selector, property_selector)?,
        }),
        _ => {
            Err(format_err!("Selector format requires exactly 3 subselectors delimited by a `:`.",))
        }
    }
}

pub fn parse_selector_file(selector_file: &Path) -> Result<Vec<Selector>, Error> {
    let selector_file = match fs::File::open(selector_file) {
        Ok(file) => file,
        Err(_) => return Err(format_err!("Failed to open selector file at configured path.",)),
    };
    let mut selector_vec = Vec::new();
    let reader = BufReader::new(selector_file);
    for line in reader.lines() {
        match line {
            Ok(line) => selector_vec.push(parse_selector(&line)?),
            Err(_) => {
                return Err(
                    format_err!("Failed to read line of selector file at configured path.",),
                )
            }
        }
    }
    Ok(selector_vec)
}

pub fn parse_selectors(selector_path: impl Into<PathBuf>) -> Result<Vec<Selector>, Error> {
    let selector_directory_path: PathBuf = selector_path.into();
    let mut selector_vec: Vec<Selector> = Vec::new();
    for entry in fs::read_dir(selector_directory_path)? {
        let entry = entry?;
        if entry.path().is_dir() {
            return Err(format_err!("Static selector directories are expected to be flat.",));
        } else {
            selector_vec.append(&mut parse_selector_file(&entry.path())?);
        }
    }
    Ok(selector_vec)
}

pub fn convert_path_selector_to_regex(selector: &Vec<PathSelectionNode>) -> Result<Regex, Error> {
    let mut regex_string = "^".to_string();
    for path_selector in selector {
        match path_selector {
            PathSelectionNode::StringPattern(string_pattern) => {
                // TODO(4601): Support regex conversion of wildcarded string literals.
                // TODO(4601): Support converting escaped char patterns into a form
                //             matched by regex.
                regex_string.push_str(&string_pattern)
            }
            PathSelectionNode::PatternMatcher(enum_pattern) => match enum_pattern {
                PatternMatcher::Wildcard => regex_string.push_str(WILDCARD_REGEX_EQUIVALENT),
                PatternMatcher::Glob => regex_string.push_str(GLOB_REGEX_EQUIVALENT),
            },
            _ => unreachable!("no expected alternative variants of the path selection node."),
        }
        regex_string.push_str("/");
    }
    regex_string.push_str("$");

    Ok(Regex::new(&regex_string)?)
}

pub fn convert_property_selector_to_regex(selector: &PropertySelector) -> Result<Regex, Error> {
    let mut regex_string = "^".to_string();

    match selector {
        PropertySelector::StringPattern(string_pattern) => {
            // TODO(4601): Support regex conversion of wildcarded string literals.
            regex_string.push_str(&string_pattern);
        }
        // NOTE: With property selectors, the wildcard is equivalent to a glob, since it
        //       unconditionally matches all entries, and there's no concept of recursion on
        //       property selection.
        PropertySelector::Wildcard(_) => regex_string.push_str(GLOB_REGEX_EQUIVALENT),
        _ => unreachable!("no expected alternative variants of the property selection node."),
    };

    regex_string.push_str("$");

    Ok(Regex::new(&regex_string)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::prelude::*;
    use tempfile::TempDir;

    #[test]
    fn canonical_component_selector_test() {
        let test_vector: Vec<(String, PathSelectionNode, PathSelectionNode, PathSelectionNode)> = vec![
            (
                "a/b/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern("b".to_string()),
                PathSelectionNode::StringPattern("c".to_string()),
            ),
            (
                "a/*/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::PatternMatcher(PatternMatcher::Wildcard),
                PathSelectionNode::StringPattern("c".to_string()),
            ),
            (
                "a/**/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::PatternMatcher(PatternMatcher::Glob),
                PathSelectionNode::StringPattern("c".to_string()),
            ),
            (
                "a/b*/c".to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern("b*".to_string()),
                PathSelectionNode::StringPattern("c".to_string()),
            ),
            (
                r#"a/b\*/c"#.to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern(r#"b\*"#.to_string()),
                PathSelectionNode::StringPattern("c".to_string()),
            ),
            (
                r#"a/\*/c"#.to_string(),
                PathSelectionNode::StringPattern("a".to_string()),
                PathSelectionNode::StringPattern(r#"\*"#.to_string()),
                PathSelectionNode::StringPattern("c".to_string()),
            ),
        ];

        for (test_string, first_path_node, second_path_node, target_component) in test_vector {
            let component_selector = parse_component_selector(&test_string).unwrap();

            match component_selector.component_moniker.as_ref().unwrap().as_slice() {
                [first, second, third] => {
                    assert_eq!(*first, first_path_node);
                    assert_eq!(*second, second_path_node);
                    assert_eq!(*third, target_component);
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
        let mut path_vec = component_selector.component_moniker.unwrap();
        assert_eq!(path_vec.pop(), Some(PathSelectionNode::StringPattern("c".to_string())));

        assert!(path_vec.is_empty());
    }

    #[test]
    fn path_components_have_spaces_as_names_selector_test() {
        let component_selector_string = " ";
        let component_selector =
            parse_component_selector(&component_selector_string.to_string()).unwrap();
        let mut path_vec = component_selector.component_moniker.unwrap();
        assert_eq!(path_vec.pop(), Some(PathSelectionNode::StringPattern(" ".to_string())));

        assert!(path_vec.is_empty());
    }

    #[test]
    fn errorful_component_selector_test() {
        let test_vector: Vec<String> = vec![
            "".to_string(),
            "a\\".to_string(),
            r#"a/b***/c"#.to_string(),
            r#"a/***/c"#.to_string(),
        ];
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
            // String literals can't have globs.
            (r#"a/b**"#.to_string(), "c".to_string()),
            // Property selector string literals cant have globs.
            (r#"a/b"#.to_string(), "c**".to_string()),
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

    #[test]
    fn successful_selector_parsing() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("a.txt"))
            .expect("create file")
            .write_all(b"a:b:c")
            .expect("writing test file");
        File::create(tempdir.path().join("b.txt"))
            .expect("create file")
            .write_all(b"**:**:*")
            .expect("writing test file");

        assert!(parse_selectors(tempdir.path()).is_ok());
    }

    #[test]
    fn unsuccessful_selector_parsing_bad_selector() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("a.txt"))
            .expect("create file")
            .write_all(b"a:b:c")
            .expect("writing test file");
        File::create(tempdir.path().join("b.txt"))
            .expect("create file")
            .write_all(b"**:**:**")
            .expect("writing test file");

        assert!(parse_selectors(tempdir.path()).is_err());
    }

    #[test]
    fn unsuccessful_selector_parsing_nonflat_dir() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("a.txt"))
            .expect("create file")
            .write_all(b"a:b:c")
            .expect("writing test file");
        File::create(tempdir.path().join("b.txt"))
            .expect("create file")
            .write_all(b"**:**:**")
            .expect("writing test file");

        std::fs::create_dir_all(tempdir.path().join("nested")).expect("make nested");
        File::create(tempdir.path().join("nested/c.txt"))
            .expect("create file")
            .write_all(b"**:**:**")
            .expect("writing test file");
        assert!(parse_selectors(tempdir.path()).is_err());
    }

    #[test]
    fn canonical_path_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the selector, and validating against that.
        let test_cases = vec![
            (r#"**:a/*/c:*"#, r#"a/b/c/"#),
            (r#"**:a/**/c:*"#, r#"a/b/g/e/d/c/"#),
            (r#"**:**:*"#, r#"a/b/\/c/d/e\*/f/"#),
            (r#"**:a/**/d/*/**:*"#, r#"a/b/\/c/d/e\*/f/"#),
        ];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let node_path = tree_selector.node_path.unwrap();
            let selector_regex = convert_path_selector_to_regex(&node_path).unwrap();
            assert!(selector_regex.is_match(string_to_match));
        }
    }

    #[test]
    fn failing_path_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the tree selector, and valdating against that.
        let test_cases = vec![
            // TODO(4601): This test case should pass when we support wildcarded string literals.
            (r#"**:a/b*/c:*"#, r#"a/bob/c/"#),
            // Failing because it's missing a required "d" directory node in the string.
            (r#"**:a/**/d/*/**:*"#, r#"a/b/\/c/e\*/f/"#),
            // Failing because the match string doesnt end at the c node.
            (r#"**:a/**/c:*"#, r#"a/b/g/e/d/c/f/"#),
        ];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let node_path = tree_selector.node_path.unwrap();
            let selector_regex = convert_path_selector_to_regex(&node_path).unwrap();
            assert!(!selector_regex.is_match(string_to_match));
        }
    }

    #[test]
    fn canonical_property_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the property of the selector, and validating against that.
        let test_cases =
            vec![(r#"**:a:*"#, r#"a"#), (r#"**:a:bob"#, r#"bob"#), (r#"**:a:\\\*"#, r#"\*"#)];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let property_selector = tree_selector.target_properties.unwrap();
            let selector_regex = convert_property_selector_to_regex(&property_selector).unwrap();
            eprintln!("{}", selector_regex.as_str());
            assert!(selector_regex.is_match(string_to_match));
        }
    }

    #[test]
    fn failing_property_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the tree selector, and valdating against that.
        let test_cases = vec![
            // TODO(4601): This test case should pass when we support wildcarded string literals.
            (r#"**:a:b*"#, r#"bob"#),
            // TODO(4601): This test case should pass when we support translating string literals
            //             with escapes. Right now, the matching selector must be
            //             r#"**:a:\\\*"#
            (r#"**:a:\*"#, r#"\*"#),
            (r#"**:a:c"#, r#"d"#),
            (r#"**:a:bob"#, r#"thebob"#),
            (r#"**:a:c"#, r#"cdog"#),
        ];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector;
            let target_properties = tree_selector.target_properties.unwrap();
            let selector_regex = convert_property_selector_to_regex(&target_properties).unwrap();
            assert!(!selector_regex.is_match(string_to_match));
        }
    }
}
