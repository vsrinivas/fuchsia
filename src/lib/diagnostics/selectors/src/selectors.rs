// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    error::*,
    parser::{self, ParsingError, VerboseError},
    validate::*,
};
use anyhow::{self, format_err};
use fidl_fuchsia_diagnostics::{
    self, ComponentSelector, PropertySelector, Selector, SelectorArgument, StringSelector,
    StringSelectorUnknown, SubtreeSelector, TreeSelector,
};
use regex::Regex;
use regex_syntax;
use std::borrow::Borrow;
use std::fs;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};

// Character used to delimit the different sections of an inspect selector,
// the component selector, the tree selector, and the property selector.
pub static SELECTOR_DELIMITER: char = ':';

// Character used to delimit nodes within a component hierarchy path.
static PATH_NODE_DELIMITER: char = '/';

// Character used to escape interperetation of this parser's "special
// characers"; *, /, :, and \.
static ESCAPE_CHARACTER: char = '\\';

static TAB_CHAR: char = '\t';
static SPACE_CHAR: char = ' ';

// Pattern used to encode wildcard.
pub(crate) static WILDCARD_SYMBOL_STR: &str = "*";
static WILDCARD_SYMBOL_CHAR: char = '*';

static RECURSIVE_WILDCARD_SYMBOL_STR: &str = "**";

// Globs will match everything along a moniker, but won't match empty strings.
static GLOB_REGEX_EQUIVALENT: &str = ".+";

// Wildcards will match anything except for an unescaped slash, since their match
// only extends to a single moniker "node".
//
// It is OK for a wildcard to match nothing when appearing as a pattern match.
// For example, "hello*" matches both "hello world" and "hello".
static WILDCARD_REGEX_EQUIVALENT: &str = r#"(\\/|[^/])*"#;

// Recursive wildcards will match anything, including an unescaped slash.
//
// It is OK for a recursive wildcard to match nothing when appearing in a pattern match.
static RECURSIVE_WILDCARD_REGEX_EQUIVALENT: &str = ".*";

/// Returns true iff a component selector uses the recursive glob.
/// Assumes the selector has already been validated.
pub fn contains_recursive_glob(component_selector: &ComponentSelector) -> bool {
    // Unwrap as a valid selector must contain these fields.
    let last_segment = component_selector.moniker_segments.as_ref().unwrap().last().unwrap();
    match last_segment {
        StringSelector::StringPattern(pattern) if pattern == RECURSIVE_WILDCARD_SYMBOL_STR => true,
        StringSelector::StringPattern(_) => false,
        StringSelector::ExactMatch(_) => false,
        StringSelectorUnknown!() => false,
    }
}

/// Extracts and validates or parses a selector from a `SelectorArgument`.
pub fn take_from_argument<E>(arg: SelectorArgument) -> Result<Selector, Error>
where
    E: for<'a> ParsingError<'a>,
{
    match arg {
        SelectorArgument::StructuredSelector(s) => {
            s.validate()?;
            Ok(s)
        }
        SelectorArgument::RawSelector(r) => parse_selector::<VerboseError>(&r),
        _ => Err(Error::InvalidSelectorArgument),
    }
}

/// Increments the CharIndices iterator and updates the token builder
/// in order to avoid processing characters being escaped by the selector.
fn handle_escaped_char(
    token_builder: &mut String,
    selection_iter: &mut std::str::CharIndices<'_>,
) -> Result<(), anyhow::Error> {
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
pub fn tokenize_string(
    untokenized_selector: &str,
    delimiter: char,
) -> Result<Vec<String>, anyhow::Error> {
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
        return Err(format_err!(
            "Cannot have empty strings delimited by {}: {}",
            delimiter,
            untokenized_selector
        ));
    }

    token_aggregator.push(curr_token_builder);
    return Ok(token_aggregator);
}

/// Converts an unparsed component selector string into a ComponentSelector.
pub fn parse_component_selector<'a, E>(
    unparsed_component_selector: &'a str,
) -> Result<ComponentSelector, ParseError>
where
    E: ParsingError<'a>,
{
    let result = parser::consuming_component_selector::<E>(&unparsed_component_selector)?;
    Ok(result.into())
}

/// Converts an unparsed Inspect selector into a ComponentSelector and TreeSelector.
pub fn parse_selector<'a, E>(unparsed_selector: &'a str) -> Result<Selector, Error>
where
    E: ParsingError<'a>,
{
    let result = parser::selector::<E>(&unparsed_selector)?;
    Ok(result.into())
}

/// Remove any comments process a quoted line.
pub fn parse_selector_file<E>(selector_file: &Path) -> Result<Vec<Selector>, Error>
where
    E: for<'a> ParsingError<'a>,
{
    let selector_file = fs::File::open(selector_file)?;
    let mut result = Vec::new();
    let reader = BufReader::new(selector_file);
    for line in reader.lines() {
        let line = line?;
        if line.is_empty() {
            continue;
        }
        if let Some(selector) = parser::selector_or_comment::<E>(&line)? {
            result.push(selector.into());
        }
    }
    Ok(result)
}

/// Loads all the selectors in the given directory.
pub fn parse_selectors<E>(directory: &Path) -> Result<Vec<Selector>, Error>
where
    E: for<'a> ParsingError<'a>,
{
    let path: PathBuf = directory.to_path_buf();
    let mut selector_vec: Vec<Selector> = Vec::new();
    for entry in fs::read_dir(path)? {
        let entry = entry?;
        if entry.path().is_dir() {
            return Err(Error::NonFlatDirectory);
        } else {
            selector_vec.append(&mut parse_selector_file::<E>(&entry.path())?);
        }
    }
    Ok(selector_vec)
}

/// Helper method for converting ExactMatch StringSelectors to regex. We must
/// escape all special characters on the behalf of the selector author when converting
/// exact matches to regex.
fn is_special_character(character: char) -> bool {
    character == ESCAPE_CHARACTER
        || character == PATH_NODE_DELIMITER
        || character == SELECTOR_DELIMITER
        || character == WILDCARD_SYMBOL_CHAR
        || character == SPACE_CHAR
        || character == TAB_CHAR
}

fn is_space_character(character: char) -> bool {
    character == SPACE_CHAR || character == TAB_CHAR
}

/// Converts a single character from a StringSelector into a format that allows it
/// selected for as a literal character in regular expression. This means that all
/// characters in the selector string, which are also regex meta characters, end up
/// being escaped.
fn convert_single_character_to_regex(token_builder: &mut String, character: char) {
    if regex_syntax::is_meta_character(character) {
        token_builder.push(ESCAPE_CHARACTER);
    }
    token_builder.push(character);
}

/// When the regular expression converter encounters a `\` escape character
/// in the selector string, it needs to express that escape in the regular expression.
/// The regular expression needs to match both the literal backslash and whatever character
/// is being `escaped` in the selector string. So this method converts a selector string
/// like `\:` into `\\:`.
// TODO(fxbug.dev/4601): Should we validate that the only characters being "escaped" in our
//             selector strings are characters that have special syntax in our selector
//             DSL?
fn convert_escaped_char_to_regex(
    token_builder: &mut String,
    selection_iter: &mut std::str::CharIndices<'_>,
) -> Result<(), Error> {
    // We have to push an additional escape for escape characters
    // since the `\` has significance in Regex that we need to escape
    // in order to have literal matching on the backslash.
    let escaped_char_option: Option<(usize, char)> = selection_iter.next();
    token_builder.push(ESCAPE_CHARACTER);
    token_builder.push(ESCAPE_CHARACTER);
    escaped_char_option
        .map(|(_, escaped_char)| convert_single_character_to_regex(token_builder, escaped_char))
        .ok_or(Error::UnmatchedEscapeCharacter)
}

/// Converts a single StringSelector into a regular expression.
///
/// If the StringSelector is a StringPattern, it interperets `\` characters
/// as escape characters that prevent `*` characters from being evaluated as pattern
/// matchers.
///
/// If the StringSelector is an ExactMatch, it will "sanitize" the exact match to
/// align with the format of sanitized text from the system. The resulting regex will
/// be a literal matcher for escape-characters followed by special characters in the
/// selector lanaguage.
fn convert_string_selector_to_regex(
    node: &StringSelector,
    wildcard_symbol_replacement: &str,
    recursive_wildcard_symbol_replacement: Option<&str>,
) -> Result<String, Error> {
    match node {
        StringSelector::StringPattern(string_pattern) => {
            if string_pattern == WILDCARD_SYMBOL_STR {
                Ok(wildcard_symbol_replacement.to_string())
            } else if string_pattern == RECURSIVE_WILDCARD_SYMBOL_STR {
                match recursive_wildcard_symbol_replacement {
                    Some(replacement) => Ok(replacement.to_string()),
                    None => Err(Error::RecursiveWildcardNotAllowed),
                }
            } else {
                let mut node_regex_builder = "(".to_string();
                let mut node_iter = string_pattern.as_str().char_indices();
                while let Some((_, selector_char)) = node_iter.next() {
                    if selector_char == ESCAPE_CHARACTER {
                        convert_escaped_char_to_regex(&mut node_regex_builder, &mut node_iter)?
                    } else if selector_char == WILDCARD_SYMBOL_CHAR {
                        node_regex_builder.push_str(wildcard_symbol_replacement);
                    } else {
                        // This enables us to accept temporarily selectors without escaped spaces.
                        if is_space_character(selector_char) {
                            node_regex_builder.push(ESCAPE_CHARACTER);
                            node_regex_builder.push(ESCAPE_CHARACTER);
                        }
                        convert_single_character_to_regex(&mut node_regex_builder, selector_char);
                    }
                }
                node_regex_builder.push_str(")");
                Ok(node_regex_builder)
            }
        }
        StringSelector::ExactMatch(string_pattern) => {
            let mut node_regex_builder = "(".to_string();
            let mut node_iter = string_pattern.as_str().char_indices();
            while let Some((_, selector_char)) = node_iter.next() {
                if is_special_character(selector_char) {
                    // In ExactMatch mode, we assume that the client wants
                    // their series of strings to be a literal match for the
                    // sanitized strings on the system. The sanitized strings
                    // are formed by escaping all special characters, so we do
                    // the same here.
                    node_regex_builder.push(ESCAPE_CHARACTER);
                    node_regex_builder.push(ESCAPE_CHARACTER);
                }
                convert_single_character_to_regex(&mut node_regex_builder, selector_char);
            }
            node_regex_builder.push_str(")");
            Ok(node_regex_builder)
        }
        _ => unreachable!("no expected alternative variants of the path selection node."),
    }
}

/// Converts a vector of StringSelectors into a string capable of constructing a
/// regular expression which matches against strings encoding paths.
///
/// NOTE: The resulting regular expression makes the assumption that all "nodes" in the
/// strings encoding paths that it will match against have been sanitized by the
/// sanitize_string_for_selectors API in this crate.
pub fn convert_path_selector_to_regex(
    selector: &[StringSelector],
    is_subtree_selector: bool,
) -> Result<String, Error> {
    let mut regex_string = "^".to_string();
    for path_selector in selector {
        // Path selectors replace wildcards with a regex that only extends to the next
        // unescaped '/' character, since we want each node to only be applied to one level
        // of the path.
        let node_regex = convert_string_selector_to_regex(
            path_selector,
            WILDCARD_REGEX_EQUIVALENT,
            Some(RECURSIVE_WILDCARD_REGEX_EQUIVALENT),
        )?;
        regex_string.push_str(&node_regex);
        regex_string.push_str("/");
    }

    if is_subtree_selector {
        regex_string.push_str(".*")
    }

    regex_string.push_str("$");

    Ok(regex_string)
}

/// Converts a single StringSelectors into a string capable of constructing a regular
/// expression which matches strings encoding a property name on a node.
///
/// NOTE: The resulting regular expression makes the assumption that the property names
/// that it will match against have been sanitized by the  sanitize_string_for_selectors API in
/// this crate.
pub fn convert_property_selector_to_regex(selector: &StringSelector) -> Result<String, Error> {
    let mut regex_string = "^".to_string();

    // Property selectors replace wildcards with GLOB like behavior since there is no
    // concept of levels/depth to properties.
    let property_regex = convert_string_selector_to_regex(selector, GLOB_REGEX_EQUIVALENT, None)?;
    regex_string.push_str(&property_regex);

    regex_string.push_str("$");

    Ok(regex_string)
}

/// Sanitizes raw strings from the system such that they align with the
/// special-character and escaping semantics of the Selector format.
///
/// Sanitization escapes the known special characters in the selector language.
///
/// NOTE: All strings must be sanitized before being evaluated by
///       selectors in regex form.
pub fn sanitize_string_for_selectors(node: &str) -> String {
    if node.is_empty() {
        return String::new();
    }

    // Preallocate enough space to store the original string.
    let mut sanitized_string = String::with_capacity(node.len());

    node.chars().for_each(|node_char| {
        if is_special_character(node_char) {
            sanitized_string.push(ESCAPE_CHARACTER);
        }
        sanitized_string.push(node_char);
    });

    sanitized_string
}

/// Sanitizes a moniker raw string such that it can be used in a selector.
/// Monikers have a restricted set of characters `a-z`, `0-9`, `_`, `.`, `-`.
/// Each moniker segment is separated by a `\`. Segments for collections also contain `:`.
/// That `:` will be escaped.
pub fn sanitize_moniker_for_selectors(moniker: &str) -> String {
    moniker.replace(":", "\\:")
}

pub fn match_moniker_against_component_selector(
    moniker: &[impl AsRef<str> + std::string::ToString],
    component_selector: &ComponentSelector,
) -> Result<bool, anyhow::Error> {
    let moniker_selector: &Vec<StringSelector> = match &component_selector.moniker_segments {
        Some(path_vec) => &path_vec,
        None => return Err(format_err!("Component selectors require moniker segments.")),
    };

    let mut sanitized_moniker = moniker
        .iter()
        .map(|s| sanitize_string_for_selectors(s.as_ref()))
        .collect::<Vec<String>>()
        .join("/");

    // We must append a "/" because the regex strings assume that all paths end
    // in a slash.
    sanitized_moniker.push('/');

    let moniker_regex = Regex::new(&convert_path_selector_to_regex(
        moniker_selector,
        /*is_subtree_selector=*/ false,
    )?)?;

    Ok(moniker_regex.is_match(&sanitized_moniker))
}

/// Evaluates a component moniker against a single selector, returning
/// True if the selector matches the component, else false.
///
/// Requires: hierarchy_path is not empty.
///           selectors contains valid Selectors.
pub fn match_component_moniker_against_selector<T>(
    moniker: &[T],
    selector: &Selector,
) -> Result<bool, anyhow::Error>
where
    T: AsRef<str> + std::string::ToString,
{
    selector.validate()?;

    if moniker.is_empty() {
        return Err(format_err!(
            "Cannot have empty monikers, at least the component name is required."
        ));
    }

    // Unwrap is safe because the validator ensures there is a component selector.
    let component_selector = selector.component_selector.as_ref().unwrap();

    match_moniker_against_component_selector(moniker, component_selector)
}

/// Evaluates a component moniker against a list of selectors, returning
/// all of the selectors which are matches for that moniker.
///
/// Requires: hierarchy_path is not empty.
///           selectors contains valid Selectors.
pub fn match_component_moniker_against_selectors<'a, T>(
    moniker: &[String],
    selectors: &'a [T],
) -> Result<Vec<&'a Selector>, anyhow::Error>
where
    T: Borrow<Selector>,
{
    if moniker.is_empty() {
        return Err(format_err!(
            "Cannot have empty monikers, at least the component name is required."
        ));
    }

    let selectors = selectors
        .iter()
        .map(|selector| {
            let component_selector = selector.borrow();
            component_selector.validate()?;
            Ok(component_selector)
        })
        .collect::<Result<Vec<&Selector>, anyhow::Error>>();

    selectors?
        .iter()
        .filter_map(|selector| {
            match_component_moniker_against_selector(moniker, selector)
                .map(|is_match| if is_match { Some(*selector) } else { None })
                .transpose()
        })
        .collect::<Result<Vec<&Selector>, anyhow::Error>>()
}

/// Evaluates a component moniker against a list of component selectors, returning
/// all of the component selectors which are matches for that moniker.
///
/// Requires: moniker is not empty.
///           component_selectors contains valid ComponentSelectors.
pub fn match_moniker_against_component_selectors<'a, T>(
    moniker: &[String],
    selectors: &'a [T],
) -> Result<Vec<&'a ComponentSelector>, anyhow::Error>
where
    T: Borrow<ComponentSelector> + 'a,
{
    if moniker.is_empty() {
        return Err(format_err!(
            "Cannot have empty monikers, at least the component name is required."
        ));
    }

    let component_selectors = selectors
        .iter()
        .map(|selector| {
            let component_selector = selector.borrow();
            component_selector.validate()?;
            Ok(component_selector)
        })
        .collect::<Result<Vec<&ComponentSelector>, anyhow::Error>>();

    component_selectors?
        .iter()
        .filter_map(|selector| {
            match_moniker_against_component_selector(moniker, selector)
                .map(|is_match| if is_match { Some(*selector) } else { None })
                .transpose()
        })
        .collect::<Result<Vec<&ComponentSelector>, anyhow::Error>>()
}

/// Format a |Selector| as a string.
///
/// Returns the formatted |Selector|, or an error if the |Selector| is invalid.
///
/// Note that the output will always include both a component and tree selector. If your input is
/// simply "moniker" you will likely see "moniker:root" as many clients implicitly append "root" if
/// it is not present (e.g. iquery).
pub fn selector_to_string(selector: Selector) -> Result<String, anyhow::Error> {
    selector.validate()?;

    let component_selector =
        selector.component_selector.ok_or_else(|| format_err!("component selector missing"))?;
    let (node_path, maybe_property_selector) = match selector
        .tree_selector
        .ok_or_else(|| format_err!("tree selector missing"))?
    {
        TreeSelector::SubtreeSelector(SubtreeSelector { node_path, .. }) => (node_path, None),
        TreeSelector::PropertySelector(PropertySelector {
            node_path, target_properties, ..
        }) => (node_path, Some(target_properties)),
        _ => return Err(format_err!("unknown tree selector type")),
    };

    let mut segments = vec![];

    let escape_special_chars = |val: &str| {
        let mut ret = String::with_capacity(val.len());
        for c in val.chars() {
            if is_special_character(c) {
                ret.push('\\');
            }
            ret.push(c);
        }
        ret
    };

    let process_string_selector_vector = |v: Vec<StringSelector>| -> Result<String, anyhow::Error> {
        Ok(v.into_iter()
            .map(|segment| match segment {
                StringSelector::StringPattern(s) => Ok(s),
                StringSelector::ExactMatch(s) => Ok(escape_special_chars(&s)),
                _ => {
                    return Err(format_err!("Unknown string selector type"));
                }
            })
            .collect::<Result<Vec<_>, anyhow::Error>>()?
            .join("/"))
    };

    // Create the component moniker
    segments.push(process_string_selector_vector(
        component_selector
            .moniker_segments
            .ok_or_else(|| format_err!("component selector missing moniker"))?,
    )?);

    // Create the node selector
    segments.push(process_string_selector_vector(node_path)?);

    if let Some(property_selector) = maybe_property_selector {
        segments.push(process_string_selector_vector(vec![property_selector])?);
    }

    Ok(segments.join(":"))
}

/// Matches a string against a single StringSelector.
/// This will only match against a single "level" and does not support recursive globbing.
pub fn match_selector_against_single_node(
    node: &impl AsRef<str>,
    selector: &StringSelector,
) -> Result<bool, anyhow::Error> {
    let regex = Regex::new(&format!(
        "^{}$",
        convert_string_selector_to_regex(selector, WILDCARD_REGEX_EQUIVALENT, None)?
    ))?;

    Ok(regex.is_match(&sanitize_string_for_selectors(node.as_ref())))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::File;
    use std::io::prelude::*;
    use tempfile::TempDir;

    #[fuchsia::test]
    fn successful_selector_parsing() {
        let tempdir = TempDir::new().expect("failed to create tmp dir");
        File::create(tempdir.path().join("a.txt"))
            .expect("create file")
            .write_all(
                b"a:b:c

",
            )
            .expect("writing test file");
        File::create(tempdir.path().join("b.txt"))
            .expect("create file")
            .write_all(b"a*/b:c/d/*:*")
            .expect("writing test file");

        File::create(tempdir.path().join("c.txt"))
            .expect("create file")
            .write_all(
                b"// this is a comment
a:b:c
",
            )
            .expect("writing test file");

        assert!(parse_selectors::<VerboseError>(tempdir.path()).is_ok());
    }

    #[fuchsia::test]
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

        assert!(parse_selectors::<VerboseError>(tempdir.path()).is_err());
    }

    #[fuchsia::test]
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
        assert!(parse_selectors::<VerboseError>(tempdir.path()).is_err());
    }

    #[fuchsia::test]
    fn canonical_path_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the selector, and validating against that.
        let test_cases = vec![
            (r#"echo.cmx:a/*/c:*"#, vec!["a", "b", "c"]),
            (r#"echo.cmx:a/*/*/*/*/c:*"#, vec!["a", "b", "g", "e", "d", "c"]),
            (r#"echo.cmx:*/*/*/*/*/*/*:*"#, vec!["a", "b", "/c", "d", "e*", "f"]),
            (r#"echo.cmx:a/*/*/d/*/*:*"#, vec!["a", "b", "/c", "d", "e*", "f"]),
            (r#"echo.cmx:a/*/\/c/d/e\*/*:*"#, vec!["a", "b", "/c", "d", "e*", "f"]),
            (r#"echo.cmx:a/b*/c:*"#, vec!["a", "bob", "c"]),
            (r#"echo.cmx:a/b/c"#, vec!["a", "b", "c"]),
            (r#"echo.cmx:a/b/c"#, vec!["a", "b", "c", "/"]),
            (r#"echo.cmx:a/b/c"#, vec!["a", "b", "c", "d"]),
            (r#"echo.cmx:a/b/c"#, vec!["a", "b", "c", "d", "e"]),
        ];
        for (selector, string_to_match) in test_cases {
            let mut sanitized_node_path = string_to_match
                .iter()
                .map(|s| sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");
            // We must append a "/" because the absolute monikers end in slash and
            // hierarchy node paths don't, but we want to reuse the regex logic.
            sanitized_node_path.push('/');

            let parsed_selector = parse_selector::<VerboseError>(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector.unwrap();
            match tree_selector {
                TreeSelector::SubtreeSelector(tree_selector) => {
                    let node_path = tree_selector.node_path;
                    let selector_regex =
                        Regex::new(&convert_path_selector_to_regex(&node_path, true).unwrap())
                            .unwrap();
                    assert!(selector_regex.is_match(&sanitized_node_path));
                }
                TreeSelector::PropertySelector(tree_selector) => {
                    let node_path = tree_selector.node_path;
                    let selector_regex =
                        Regex::new(&convert_path_selector_to_regex(&node_path, false).unwrap())
                            .unwrap();
                    assert!(selector_regex.is_match(&sanitized_node_path));
                }
                _ => unreachable!(),
            }
        }
    }

    #[fuchsia::test]
    fn failing_path_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the tree selector, and valdating against that.
        let test_cases = vec![
            // Failing because it's missing a required "d" directory node in the string.
            (r#"echo.cmx:a/*/d/*/f:*"#, vec!["a", "b", "c", "e", "f"]),
            // Failing because the match string doesn't end at the c node.
            (r#"echo.cmx:a/*/*/*/*/*/c:*"#, vec!["a", "b", "g", "e", "d", "f"]),
            (r#"echo.cmx:a/b/c"#, vec!["a", "b"]),
            (r#"echo.cmx:a/b/c"#, vec!["a", "b", "card"]),
            (r#"echo.cmx:a/b/c"#, vec!["a", "b", "c/"]),
        ];
        for (selector, string_to_match) in test_cases {
            let mut sanitized_node_path = string_to_match
                .iter()
                .map(|s| sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");
            // We must append a "/" because the absolute monikers end in slash and
            // hierarchy node paths don't, but we want to reuse the regex logic.
            sanitized_node_path.push('/');

            let parsed_selector = parse_selector::<VerboseError>(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector.unwrap();
            match tree_selector {
                TreeSelector::SubtreeSelector(tree_selector) => {
                    let node_path = tree_selector.node_path;
                    let selector_regex =
                        Regex::new(&convert_path_selector_to_regex(&node_path, true).unwrap())
                            .unwrap();
                    assert!(!selector_regex.is_match(&sanitized_node_path));
                }
                TreeSelector::PropertySelector(tree_selector) => {
                    let node_path = tree_selector.node_path;
                    let selector_regex =
                        Regex::new(&convert_path_selector_to_regex(&node_path, false).unwrap())
                            .unwrap();
                    assert!(!selector_regex.is_match(&sanitized_node_path));
                }
                _ => unreachable!(),
            }
        }
    }

    #[fuchsia::test]
    fn canonical_property_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the property of the selector, and validating against that.
        let test_cases = vec![
            (r#"echo.cmx:a:*"#, r#"a"#),
            (r#"echo.cmx:a:bob"#, r#"bob"#),
            (r#"echo.cmx:a:b*"#, r#"bob"#),
            (r#"echo.cmx:a:\*"#, r#"*"#),
            (r#"echo.cmx:a:b\ c"#, r#"b c"#),
        ];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector::<VerboseError>(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector.unwrap();
            match tree_selector {
                TreeSelector::SubtreeSelector(_) => {
                    unreachable!("Subtree selectors don't test property selection.")
                }
                TreeSelector::PropertySelector(tree_selector) => {
                    let property_selector = tree_selector.target_properties;
                    let selector_regex = Regex::new(
                        &convert_property_selector_to_regex(&property_selector).unwrap(),
                    )
                    .unwrap();
                    assert!(
                        selector_regex.is_match(&sanitize_string_for_selectors(string_to_match)),
                        "{} failed for {} with regex={:?}",
                        selector,
                        string_to_match,
                        selector_regex
                    );
                }
                _ => unreachable!(),
            }
        }
    }

    #[fuchsia::test]
    fn failing_property_regex_transpilation_test() {
        // Note: We provide the full selector syntax but this test is only transpiling
        // the node-path of the tree selector, and valdating against that.
        let test_cases = vec![
            (r#"echo.cmx:a:c"#, r#"d"#),
            (r#"echo.cmx:a:bob"#, r#"thebob"#),
            (r#"echo.cmx:a:c"#, r#"cdog"#),
        ];
        for (selector, string_to_match) in test_cases {
            let parsed_selector = parse_selector::<VerboseError>(selector).unwrap();
            let tree_selector = parsed_selector.tree_selector.unwrap();
            match tree_selector {
                TreeSelector::SubtreeSelector(_) => {
                    unreachable!("Subtree selectors don't test property selection.")
                }
                TreeSelector::PropertySelector(tree_selector) => {
                    let target_properties = tree_selector.target_properties;
                    let selector_regex = Regex::new(
                        &convert_property_selector_to_regex(&target_properties).unwrap(),
                    )
                    .unwrap();
                    assert!(
                        !selector_regex.is_match(&sanitize_string_for_selectors(string_to_match))
                    );
                }
                _ => unreachable!(),
            }
        }
    }

    #[fuchsia::test]
    fn component_selector_match_test() {
        // Note: We provide the full selector syntax but this test is only validating it
        // against the provided moniker
        let passing_test_cases = vec![
            (r#"echo.cmx:*:*"#, vec!["echo.cmx"]),
            (r#"*/echo.cmx:*:*"#, vec!["abc", "echo.cmx"]),
            (r#"ab*/echo.cmx:*:*"#, vec!["abc", "echo.cmx"]),
            (r#"ab*/echo.cmx:*:*"#, vec!["abcde", "echo.cmx"]),
            (r#"*/ab*/echo.cmx:*:*"#, vec!["123", "abcde", "echo.cmx"]),
            (r#"echo.cmx*:*:*"#, vec!["echo.cmx"]),
            (r#"a/echo*.cmx:*:*"#, vec!["a", "echo1.cmx"]),
            (r#"a/echo*.cmx:*:*"#, vec!["a", "echo.cmx"]),
            (r#"ab*/echo.cmx:*:*"#, vec!["ab", "echo.cmx"]),
            (r#"a/**:*:*"#, vec!["a", "echo.cmx"]),
            (r#"a/**:*:*"#, vec!["a", "b", "echo.cmx"]),
        ];

        for (selector, moniker) in passing_test_cases {
            let parsed_selector = parse_selector::<VerboseError>(selector).unwrap();
            assert!(
                match_component_moniker_against_selector(&moniker, &parsed_selector).unwrap(),
                "Selector {:?} failed to match {:?}",
                selector,
                moniker
            );
        }

        // Note: We provide the full selector syntax but this test is only validating it
        // against the provided moniker
        let failing_test_cases = vec![
            (r#"*:*:*"#, vec!["a", "echo.cmx"]),
            (r#"*/echo.cmx:*:*"#, vec!["123", "abc", "echo.cmx"]),
            (r#"a/**:*:*"#, vec!["b", "echo.cmx"]),
            (r#"e/**:*:*"#, vec!["echo.cmx"]),
        ];

        for (selector, moniker) in failing_test_cases {
            let parsed_selector = parse_selector::<VerboseError>(selector).unwrap();
            assert!(
                !match_component_moniker_against_selector(&moniker, &parsed_selector).unwrap(),
                "Selector {:?} matched {:?}, but was expected to fail",
                selector,
                moniker
            );
        }
    }

    #[fuchsia::test]
    fn multiple_component_selectors_match_test() {
        let selectors = vec![r#"*/echo.cmx"#, r#"ab*/echo.cmx"#, r#"abc/m*"#];
        let moniker = vec!["abc".to_string(), "echo.cmx".to_string()];

        let component_selectors = selectors
            .into_iter()
            .map(|selector| {
                parse_component_selector::<VerboseError>(&selector.to_string()).unwrap()
            })
            .collect::<Vec<_>>();

        let match_res =
            match_moniker_against_component_selectors(moniker.as_slice(), &component_selectors[..]);
        assert!(match_res.is_ok());
        assert_eq!(match_res.unwrap().len(), 2);
    }

    #[fuchsia::test]
    fn selector_to_string_test() {
        // Check that parsing and formatting these selectors results in output identical to the
        // original selector.
        let cases = vec![
            r#"moniker:root"#,
            r#"my/component:root"#,
            r#"my/component:root:a"#,
            r#"a/b/c*ff:root:a"#,
            r#"a/child*:root:a"#,
            r#"a/child:root/a/b/c"#,
            r#"a/child:root/a/b/c:d"#,
            r#"a/child:root/a/b/c*/d"#,
            r#"a/child:root/a/b/c\*/d"#,
            r#"a/child:root/a/b/c:d*"#,
            r#"a/child:root/a/b/c:*d*"#,
            r#"a/child:root/a/b/c:\*d*"#,
            r#"a/child:root/a/b/c:\*d\:\*\\"#,
        ];

        for input in cases {
            let selector = parse_selector::<VerboseError>(input)
                .unwrap_or_else(|e| panic!("Failed to parse '{}': {}", input, e));
            let output = selector_to_string(selector).unwrap_or_else(|e| {
                panic!("Failed to format parsed selector for '{}': {}", input, e)
            });
            assert_eq!(output, input);
        }
    }

    #[fuchsia::test]
    fn exact_match_selector_to_string() {
        let selector = Selector {
            component_selector: Some(ComponentSelector {
                moniker_segments: Some(vec![StringSelector::ExactMatch("a".to_string())]),
                ..ComponentSelector::EMPTY
            }),
            tree_selector: Some(TreeSelector::SubtreeSelector(SubtreeSelector {
                node_path: vec![StringSelector::ExactMatch("a*:".to_string())],
            })),
            ..Selector::EMPTY
        };

        // Check we generate the expected string with escaping.
        let selector_string = selector_to_string(selector).unwrap();
        assert_eq!(r#"a:a\*\:"#, selector_string);

        // Parse the resultant selector, and check that it matches a moniker it is supposed to.
        let parsed = parse_selector::<VerboseError>(&selector_string).unwrap();
        assert!(match_moniker_against_component_selector(
            &["a"],
            parsed.component_selector.as_ref().unwrap()
        )
        .unwrap());
    }

    #[fuchsia::test]
    fn sanitize_moniker_for_selectors_result_is_usable() {
        let selector = parse_selector::<VerboseError>(&format!(
            "{}:root",
            sanitize_moniker_for_selectors("foo/coll:bar/baz")
        ))
        .unwrap();
        let component_selector = selector.component_selector.as_ref().unwrap();
        let moniker = vec!["foo".to_string(), "coll:bar".to_string(), "baz".to_string()];
        assert!(match_moniker_against_component_selector(&moniker, &component_selector).unwrap());
    }

    #[fuchsia::test]
    fn escaped_spaces() {
        let selector_str = "foo:bar\\ baz/a*\\ b:quux";
        let selector = parse_selector::<VerboseError>(selector_str).unwrap();
        assert_eq!(
            selector,
            Selector {
                component_selector: Some(ComponentSelector {
                    moniker_segments: Some(vec![StringSelector::ExactMatch("foo".into()),]),
                    ..ComponentSelector::EMPTY
                }),
                tree_selector: Some(TreeSelector::PropertySelector(PropertySelector {
                    node_path: vec![
                        StringSelector::ExactMatch("bar baz".into()),
                        StringSelector::StringPattern("a*\\ b".into()),
                    ],
                    target_properties: StringSelector::ExactMatch("quux".into())
                })),
                ..Selector::EMPTY
            }
        );
    }
}
