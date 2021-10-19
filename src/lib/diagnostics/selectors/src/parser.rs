// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/55118): remove.
#![allow(dead_code)]

use crate::types::*;
use nom::{
    self,
    branch::alt,
    bytes::complete::{escaped, is_not, tag, take_while, take_while1},
    character::complete::{alphanumeric1, char, digit1, hex_digit1, multispace0, none_of, one_of},
    combinator::{
        all_consuming, complete, cond, map, map_opt, map_res, opt, peek, recognize, verify,
    },
    error::{ErrorKind, ParseError},
    multi::{many0, many1, separated_nonempty_list},
    sequence::{delimited, preceded, tuple},
    IResult,
};

macro_rules! comparison {
    ($tag:literal, $variant:ident) => {
        map(tag($tag), move |_| ComparisonOperator::$variant)
    };
}

/// Parses comparison operators.
fn comparison(input: &str) -> IResult<&str, ComparisonOperator> {
    alt((
        comparison!("=", Equal),
        comparison!(">=", GreaterEq),
        comparison!(">", Greater),
        comparison!("<=", LessEq),
        comparison!("<", Less),
        comparison!("!=", NotEq),
    ))(input)
}

/// Recognizes 1 or more spaces or tabs.
fn whitespace1<'a, E: ParseError<&'a str>>(input: &'a str) -> IResult<&'a str, &'a str, E> {
    take_while1(move |c| c == ' ' || c == '\t')(input)
}

/// Recognizes 0 or more spaces or tabs.
fn whitespace0<'a, E: ParseError<&'a str>>(input: &'a str) -> IResult<&'a str, &'a str, E> {
    take_while(move |c| c == ' ' || c == '\t')(input)
}

/// Parses the `has any` operator in the two flavors we support: all characters uppercase or all
/// of them lowercase.
fn has_any(input: &str) -> IResult<&str, (&str, &str)> {
    let (rest, (has, _, any)) = alt((
        tuple((tag("has"), whitespace1, tag("any"))),
        tuple((tag("HAS"), whitespace1, tag("ANY"))),
    ))(input)?;
    Ok((rest, (has, any)))
}

/// Parses the `has all` operator in the two flavors we support: all characters uppercase or all
/// of them lowercase.
fn has_all(input: &str) -> IResult<&str, (&str, &str)> {
    let (rest, (has, _, all)) = alt((
        tuple((tag("has"), whitespace1, tag("all"))),
        tuple((tag("HAS"), whitespace1, tag("ALL"))),
    ))(input)?;
    Ok((rest, (has, all)))
}

/// Parses any inclusion operator (`has any`, `has all`, `in`) in the two flavors we support: all
/// characters uppercase or all of them lowercase.
fn inclusion(input: &str) -> IResult<&str, InclusionOperator> {
    alt((
        map(has_any, move |_| InclusionOperator::HasAny),
        map(has_all, move |_| InclusionOperator::HasAll),
        map(alt((tag("in"), tag("IN"))), move |_| InclusionOperator::In),
    ))(input)
}

/// Parses any operator (inclusion and comparison).
fn operator(input: &str) -> IResult<&str, Operator> {
    alt((
        map(inclusion, move |o| Operator::Inclusion(o)),
        map(comparison, move |o| Operator::Comparison(o)),
    ))(input)
}

// This macro generates:
// - A general parser function `$parser` for all the given `tags`.
// - Parser functions `$tag_parser` that parses inputs that match the given accepted `$tag`s into
//   `Severity`.
// - A function `severity_sym` that parses an input into a `Severity`.
macro_rules! reserved {
    (
        parser: $parser:ident,
        ty: $type:ident,
        tags: [
            $({
                parser: $tag_parser:ident,
                variant: $variant_name:ident,
                accepts: [$($tag:ident),+]
            }),+
        ]
    ) => {
        $(
            fn $tag_parser(input: &str) -> IResult<&str, $type> {
                map(alt(($(tag(stringify!($tag))),+)), move |_| $type::$variant_name)(input)
            }
        )+

        fn $parser(input: &str) -> IResult<&str, $type> {
            alt(($( $tag_parser ),+))(input)
        }
    };
}

reserved!(
    parser: severity_sym,
    ty: Severity,
    tags: [
      {
        parser: trace,
        variant: Trace,
        accepts: [ trace, Trace, TRACE ]
      },
      {
        parser: debug,
        variant: Debug,
        accepts: [ debug, Debug, DEBUG ]
      },
      {
        parser: info,
        variant: Info,
        accepts: [ info, Info, INFO ]
      },
      {
        parser: warn,
        variant: Warn,
        accepts: [ warn, Warn, WARN ]
      },
      {
        parser: error,
        variant: Error,
        accepts: [ error, Error, ERROR ]
      },
      {
        parser: fatal,
        variant: Fatal,
        accepts: [ fatal, Fatal, FATAL ]
      }
    ]
);

reserved!(
    parser: identifier,
    ty: Identifier,
    tags: [
      {
        parser: filename,
        variant: Filename,
        accepts: [ filename, Filename, FILENAME ]
      },
      {
        parser: lifecycle_event_type,
        variant: LifecycleEventType,
        accepts: [ lifecycle_event_type, LifecycleEventType, lifecycleEventType, LIFECYCLE_EVENT_TYPE ]
      },
      {
        parser: line_number,
        variant: LineNumber,
        accepts: [ line_number, LineNumber, lineNumber, LINE_NUMBER ]
      },
      {
        parser: pid,
        variant: Pid,
        accepts: [ pid, Pid, PID ]
      },
      {
        parser: severity,
        variant: Severity,
        accepts: [ severity, Severity, SEVERITY ]
      },
      {
        parser: tags,
        variant: Tags,
        accepts: [ tags, Tags, TAGS ]
      },
      {
        parser: tid,
        variant: Tid,
        accepts: [ tid, Tid, TID ]
      },
      {
        parser: timestamp,
        variant: Timestamp,
        accepts: [ timestamp, Timestamp, TIMESTAMP ]
      }
    ]
);

// This macro is purely a utility fo validating that all the values in the given `$one_or_many` are
// of a given type.
macro_rules! match_one_or_many_value {
    ($one_or_many:ident, $variant:pat) => {
        match $one_or_many {
            OneOrMany::One($variant) => true,
            OneOrMany::Many(values) => values.iter().all(|value| matches!(value, $variant)),
            _ => false,
        }
    };
}

impl Identifier {
    /// Validates that all the values are of a type that can be used in an operation with this
    /// identifier.
    fn can_be_used_with_value_type(&self, value: &OneOrMany<Value<'_>>) -> bool {
        match (self, value) {
            (Identifier::Filename | Identifier::LifecycleEventType | Identifier::Tags, value) => {
                match_one_or_many_value!(value, Value::StringLiteral(_))
            }
            // TODO(fxbug.dev/55118): similar to severities, we can probably have reserved values
            // for lifecycle event types.
            // TODO(fxbug.dev/55118): support time diferences (1h30m, 30s, etc) instead of only
            // timestamp comparison.
            (
                Identifier::Pid | Identifier::Tid | Identifier::LineNumber | Identifier::Timestamp,
                value,
            ) => {
                match_one_or_many_value!(value, Value::Number(_))
            }
            // TODO(fxbug.dev/55118): it should also be possible to compare severities with a fixed
            // set of numbers.
            (Identifier::Severity, value) => {
                match_one_or_many_value!(value, Value::Severity(_))
            }
        }
    }

    /// Validates that this identifier can be used in an operation defined by the given `operator`.
    fn can_be_used_with_operator(&self, operator: &Operator) -> bool {
        match (self, &operator) {
            (
                Identifier::Filename
                | Identifier::LifecycleEventType
                | Identifier::Pid
                | Identifier::Tid
                | Identifier::LineNumber
                | Identifier::Severity,
                Operator::Comparison(ComparisonOperator::Equal)
                | Operator::Comparison(ComparisonOperator::NotEq)
                | Operator::Inclusion(InclusionOperator::In),
            ) => true,
            (Identifier::Severity | Identifier::Timestamp, Operator::Comparison(_)) => true,
            (
                Identifier::Tags,
                Operator::Inclusion(InclusionOperator::HasAny | InclusionOperator::HasAll),
            ) => true,
            _ => false,
        }
    }
}

/// Parses an input containing any number and type of whitespace at the front.
fn spaced<'a, E, F, O>(parser: F) -> impl Fn(&'a str) -> IResult<&'a str, O, E>
where
    F: Fn(&'a str) -> IResult<&'a str, O, E>,
    E: ParseError<&'a str>,
{
    preceded(whitespace0, parser)
}

/// Parses the input as a string literal wrapped in double quotes. Returns the value
/// inside the quotes.
fn string_literal(input: &str) -> IResult<&str, &str> {
    // TODO(fxbug.dev/55118): this doesn't handle escape sequences.
    // Consider accepting escaped `"`: `\"` too.
    let (rest, value) = recognize(delimited(char('"'), many0(is_not("\"")), char('"')))(input)?;
    Ok((rest, &value[1..value.len() - 1]))
}

/// Parses a 64 bit unsigned integer.
fn integer(input: &str) -> IResult<&str, u64> {
    map_res(digit1, |s: &str| s.parse::<u64>())(input)
}

/// Parses a hexadecimal number as 64 bit integer.
fn hex_integer(input: &str) -> IResult<&str, u64> {
    map_res(preceded(tag("0x"), hex_digit1), |s: &str| u64::from_str_radix(&s, 16))(input)
}

/// Parses a unsigned decimal and hexadecimal numbers of 64 bits.
/// For example: 123, 0x7b will be accepted as
// TODO(fxbug.dev/55118): consider also accepting signed numbers.
fn number(input: &str) -> IResult<&str, u64> {
    alt((hex_integer, integer))(input)
}

// This macro parses a list of expressions accepted by the given `$parser` comma separated with any
// number of spaces in between.
macro_rules! comma_separated_value {
    ($parser:ident, $value:expr) => {
        map(separated_nonempty_list(spaced(char(',')), spaced($parser)), move |ns| {
            ns.into_iter().map(|n| $value(n)).collect()
        })
    };
}

/// Parses a list of values. Each list can only contain values of the same type:
/// - String literal
/// - Number
/// - Severity
fn list_of_values(input: &str) -> IResult<&str, Vec<Value<'_>>> {
    delimited(
        spaced(char('[')),
        alt((
            comma_separated_value!(number, Value::Number),
            comma_separated_value!(string_literal, Value::StringLiteral),
            comma_separated_value!(severity_sym, Value::Severity),
        )),
        spaced(char(']')),
    )(input)
}

/// Parses a single filter expression in a metadata selector.
fn filter_expression(input: &str) -> IResult<&str, FilterExpression<'_>> {
    let (rest, identifier) = spaced(identifier)(input)?;
    let (rest, op) =
        verify(spaced(operator), |op| identifier.can_be_used_with_operator(&op))(rest)?;
    let (rest, op) = map_opt(
        verify(
            spaced(alt((
                map(number, move |n| OneOrMany::One(Value::Number(n))),
                map(severity_sym, move |s| OneOrMany::One(Value::Severity(s))),
                map(string_literal, move |s| OneOrMany::One(Value::StringLiteral(s))),
                map(list_of_values, OneOrMany::Many),
            ))),
            |value| identifier.can_be_used_with_value_type(value),
        ),
        move |one_or_many| Operation::maybe_new(op, one_or_many),
    )(rest)?;
    Ok((rest, FilterExpression { identifier, op }))
}

/// Parses a metadata selector.
fn metadata_selector(input: &str) -> IResult<&str, MetadataSelector<'_>> {
    let (rest, _) = spaced(alt((tag("WHERE"), tag("where"))))(input)?;
    let (rest, filters) = spaced(separated_nonempty_list(char(','), filter_expression))(rest)?;
    Ok((rest, MetadataSelector::new(filters)))
}

/// Parses a tree selector, which is a node selector and an optional property selector.
fn tree_selector<'a>(
    accept_spaces: bool,
) -> impl Fn(&'a str) -> IResult<&'a str, TreeSelector<'a>> {
    move |input: &str| {
        let esc = if accept_spaces {
            escaped(none_of(":/\"\\\n"), '\\', one_of(r#"/:\""#))
        } else {
            escaped(none_of(":/\\ \t\n"), '\\', one_of(r#"/:\"#))
        };
        let (rest, node_segments) =
            verify(separated_nonempty_list(tag("/"), &esc), |segments: &Vec<&str>| {
                !segments.iter().any(|s| s.contains("**"))
            })(input)?;
        let (rest, property_segment) =
            if peek::<&str, _, (&str, ErrorKind), _>(tag(":"))(rest).is_ok() {
                let (rest, _) = tag(":")(rest)?;
                let (rest, property) =
                    verify(esc, |value: &str| !value.is_empty() && !value.contains("**"))(rest)?;
                (rest, Some(property))
            } else {
                (rest, None)
            };
        Ok((
            rest,
            TreeSelector {
                node: node_segments.into_iter().map(|value| value.into()).collect(),
                property: property_segment.map(|value| value.into()),
            },
        ))
    }
}

impl<'a> Into<Segment<'a>> for &'a str {
    fn into(self) -> Segment<'a> {
        if self.contains('*') {
            Segment::Pattern(self)
        } else {
            Segment::Exact(self)
        }
    }
}

/// Parses a component selector.
fn component_selector(input: &str) -> IResult<&str, ComponentSelector<'_>> {
    let accepted_characters = alt((alphanumeric1, tag("*"), tag("."), tag("-"), tag("_")));
    let (rest, segments) = verify(
        separated_nonempty_list(tag("/"), recognize(many1(accepted_characters))),
        |segments: &Vec<&str>| {
            // TODO: it's probably possible to write this more cleanly as a combinator.
            segments.iter().enumerate().all(|(i, segment)| {
                if segment.contains("**") {
                    if i == segments.len() - 1 {
                        // The last segment can be the recursive glob, but nothing else.
                        return *segment == "**";
                    }
                    // Other segments aren't allowed to contain recursive globs.
                    return false;
                }
                true
            })
        },
    )(input)?;
    Ok((rest, ComponentSelector { segments: segments.into_iter().map(|s| s.into()).collect() }))
}

/// A comment allowed in selector files.
fn comment(input: &str) -> IResult<&str, &str> {
    let (rest, comment) = spaced(preceded(tag("//"), is_not("\n\r")))(input)?;
    if rest.len() > 0 {
        let (rest, _) = one_of("\n\r")(rest)?; // consume the newline character
        return Ok((rest, comment));
    }
    Ok((rest, comment))
}

/// Parses a core selector (component + tree + property). It accepts both raw selectors or
/// selectors wrapped in double quotes. Selectors wrapped in quotes accept spaces in the tree and
/// property names and require internal quotes to be escaped.
fn core_selector(input: &str) -> IResult<&str, (ComponentSelector<'_>, TreeSelector<'_>)> {
    if peek::<&str, _, (&str, ErrorKind), _>(tag("\""))(input).is_ok() {
        let (rest, (_, component, _, tree, _)) = tuple((
            tag("\""),
            component_selector,
            tag(":"),
            tree_selector(/*accept_spaces=*/ true),
            tag("\""),
        ))(input)?;
        Ok((rest, (component, tree)))
    } else {
        let (rest, (component, _, tree)) =
            tuple((component_selector, tag(":"), tree_selector(/*accept_spaces=*/ false)))(input)?;
        Ok((rest, (component, tree)))
    }
}

/// Recognizes selectors, with comments allowed or disallowed.
fn do_parse_selector<'a>(
    allow_inline_comment: bool,
) -> impl Fn(&'a str) -> IResult<&'a str, Selector<'a>, (&'a str, ErrorKind)> {
    map(
        tuple((
            spaced(core_selector),
            opt(metadata_selector),
            cond(allow_inline_comment, opt(comment)),
            multispace0,
        )),
        move |((component, tree), metadata, _, _)| Selector { component, tree, metadata },
    )
}

/// Parses the input into a `Selector`.
pub fn selector(input: &str) -> Result<Selector<'_>, (&str, ErrorKind)> {
    let result = complete(all_consuming(do_parse_selector(/*allow_inline_comment=*/ false)))(input);
    match result {
        Ok((_, s)) => Ok(s),
        Err(nom::Err::Error(e) | nom::Err::Failure(e)) => Err(e),
        _ => unreachable!("through the complete combinator we get rid of Incomplete"),
    }
}

/// Parses a newline-separated list of selectors. Each line might contain comments and selectors
/// are allowed to contain inline comments as well.
pub fn selector_list(input: &str) -> Result<Vec<Selector<'_>>, (&str, ErrorKind)> {
    let result = complete(all_consuming(many1(alt((
        map(comment, |_| None),
        map(do_parse_selector(/*allow_inline_comment=*/ true), |s| Some(s)),
    )))))(input);
    match result {
        Ok((_, matches)) => Ok(matches.into_iter().filter_map(|s| s).collect()),
        Err(nom::Err::Error(e) | nom::Err::Failure(e)) => Err(e),
        _ => unreachable!("through the complete combinator we get rid of Incomplete"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nom::combinator::all_consuming;
    use rand::distributions::Distribution;

    #[test]
    fn canonical_component_selector_test() {
        let test_vector = vec![
            ("a/b/c", vec![Segment::Exact("a"), Segment::Exact("b"), Segment::Exact("c")]),
            ("a/*/c", vec![Segment::Exact("a"), Segment::Pattern("*"), Segment::Exact("c")]),
            ("a/b*/c", vec![Segment::Exact("a"), Segment::Pattern("b*"), Segment::Exact("c")]),
            ("a/b/**", vec![Segment::Exact("a"), Segment::Exact("b"), Segment::Pattern("**")]),
            ("c", vec![Segment::Exact("c")]),
            (
                r#"a/*/b/**"#,
                vec![
                    Segment::Exact("a"),
                    Segment::Pattern("*"),
                    Segment::Exact("b"),
                    Segment::Pattern("**"),
                ],
            ),
        ];

        for (test_string, expected_segments) in test_vector {
            let (_, component_selector) = component_selector(&test_string).unwrap();

            assert_eq!(
                expected_segments, component_selector.segments,
                "For '{}', got: {:?}",
                test_string, component_selector,
            );
        }
    }

    #[test]
    fn missing_path_component_selector_test() {
        let component_selector_string = "c";
        let (_, component_selector) = component_selector(component_selector_string).unwrap();
        let mut path_vec = component_selector.segments;
        assert_eq!(path_vec.pop(), Some(Segment::Exact("c")));
        assert!(path_vec.is_empty());
    }

    #[test]
    fn errorful_component_selector_test() {
        let test_vector: Vec<&str> = vec![
            "",
            "a\\",
            r#"a/b***/c"#,
            r#"a/***/c"#,
            r#"a/**/c"#,
            // NOTE: This used to be accepted but not anymore. Spaces shouldn't be a valid component
            // selector character since it's not a valid moniker character.
            " ",
            // NOTE: The previous parser was accepting quotes in component selectors. However, by
            // definition, a component moniker (both in v1 and v2) doesn't allow a `*` in its name.
            r#"a/b\*/c"#,
            r#"a/\*/c"#,
            // Invalid characters
            "a$c/d",
        ];
        for test_string in test_vector {
            let component_selector_result = all_consuming(component_selector)(test_string);
            assert!(component_selector_result.is_err(), "expected '{}' to fail", test_string);
        }
    }

    #[test]
    fn canonical_tree_selector_test() {
        let test_vector = vec![
            ("a/b:c", vec![Segment::Exact("a"), Segment::Exact("b")], Some(Segment::Exact("c"))),
            ("a/*:c", vec![Segment::Exact("a"), Segment::Pattern("*")], Some(Segment::Exact("c"))),
            ("a/b:*", vec![Segment::Exact("a"), Segment::Exact("b")], Some(Segment::Pattern("*"))),
            ("a/b", vec![Segment::Exact("a"), Segment::Exact("b")], None),
            (r#"a/b\:c"#, vec![Segment::Exact("a"), Segment::Exact(r#"b\:c"#)], None),
        ];

        for (string, expected_path, expected_property) in test_vector {
            let (_, tree_selector) = tree_selector(false)(string).unwrap();
            assert_eq!(
                tree_selector,
                TreeSelector { node: expected_path, property: expected_property }
            );
        }
    }

    #[test]
    fn errorful_tree_selector_test() {
        let test_vector = vec![
            // Not allowed due to empty property selector.
            "a/b:",
            // Not allowed due to glob property selector.
            "a/b:**",
            // String literals can't have globs.
            r#"a/b**:c"#,
            // Property selector string literals cant have globs.
            r#"a/b:c**"#,
            "a/b:**",
            // Node path cant have globs.
            "a/**:c",
            // Node path can't be empty
            ":c",
            // Spaces aren't accepted when parsing with allow_spaces=false.
            "a b:c",
            "a*b:\tc",
        ];
        for string in test_vector {
            assert!(all_consuming(tree_selector(false))(string).is_err(), "{} should fail", string);
        }
    }

    #[test]
    fn tree_selector_with_spaces() {
        let with_spaces = vec![
            ("a b:c", vec![Segment::Exact("a b")], Some(Segment::Exact("c"))),
            (
                "ab/ d:c ",
                vec![Segment::Exact("ab"), Segment::Exact(" d")],
                Some(Segment::Exact("c ")),
            ),
            ("a\t*b:c", vec![Segment::Pattern("a\t*b")], Some(Segment::Exact("c"))),
            (r#"a \"x\":c"#, vec![Segment::Exact(r#"a \"x\""#)], Some(Segment::Exact("c"))),
        ];
        for (string, node, property) in with_spaces {
            assert_eq!(
                all_consuming(tree_selector(true))(string).unwrap().1,
                TreeSelector { node, property }
            );
        }

        // Un-escaped quotes aren't accepted when parsing with spaces.
        assert!(all_consuming(tree_selector(true))(r#"a/b:"xc"/d"#).is_err());
    }

    #[test]
    fn parse_full_selector() {
        assert_eq!(
            selector(
                "core/**:some-node/he*re:prop where filename = \"baz\", severity in [info, error]"
            )
            .unwrap(),
            Selector {
                component: ComponentSelector {
                    segments: vec![Segment::Exact("core"), Segment::Pattern("**"),],
                },
                tree: TreeSelector {
                    node: vec![Segment::Exact("some-node"), Segment::Pattern("he*re"),],
                    property: Some(Segment::Exact("prop")),
                },
                metadata: Some(MetadataSelector::new(vec![
                    FilterExpression {
                        identifier: Identifier::Filename,
                        op: Operation::Comparison(
                            ComparisonOperator::Equal,
                            Value::StringLiteral("baz")
                        ),
                    },
                    FilterExpression {
                        identifier: Identifier::Severity,
                        op: Operation::Inclusion(
                            InclusionOperator::In,
                            vec![Value::Severity(Severity::Info), Value::Severity(Severity::Error)]
                        ),
                    },
                ])),
            }
        );

        // Parses selectors without metadata. Also ignores whitespace.
        assert_eq!(
            selector("   foo:bar  ").unwrap(),
            Selector {
                component: ComponentSelector { segments: vec![Segment::Exact("foo")] },
                tree: TreeSelector { node: vec![Segment::Exact("bar")], property: None },
                metadata: None,
            }
        );

        // At least one filter is required when `where` is provided.
        assert!(selector("foo:bar where").is_err());
    }

    #[test]
    fn parse_full_selector_with_spaces() {
        assert_eq!(
            selector(r#""core/foo:some node/*:prop" where pid = 123"#).unwrap(),
            Selector {
                component: ComponentSelector {
                    segments: vec![Segment::Exact("core"), Segment::Exact("foo"),],
                },
                tree: TreeSelector {
                    node: vec![Segment::Exact("some node"), Segment::Pattern("*"),],
                    property: Some(Segment::Exact("prop")),
                },
                metadata: Some(MetadataSelector::new(vec![FilterExpression {
                    identifier: Identifier::Pid,
                    op: Operation::Comparison(ComparisonOperator::Equal, Value::Number(123)),
                },])),
            }
        );
    }

    #[test]
    fn parse_selector_list() {
        let input = "// this is a comment
          foo/bar:baz where pid = 123  // inline comment for a selector starting with whitespace
          //this is a comment starting with whitespace
core/**:quux:rust\t// another inline comment
      \"core/foo:bar baz:quux\" where severity in [info, error]
        ";
        let selectors = selector_list(input).unwrap();
        assert_eq!(
            selectors,
            vec![
                Selector {
                    component: ComponentSelector {
                        segments: vec![Segment::Exact("foo"), Segment::Exact("bar"),],
                    },
                    tree: TreeSelector { node: vec![Segment::Exact("baz")], property: None },
                    metadata: Some(MetadataSelector::new(vec![FilterExpression {
                        identifier: Identifier::Pid,
                        op: Operation::Comparison(ComparisonOperator::Equal, Value::Number(123)),
                    },])),
                },
                Selector {
                    component: ComponentSelector {
                        segments: vec![Segment::Exact("core"), Segment::Pattern("**"),],
                    },
                    tree: TreeSelector {
                        node: vec![Segment::Exact("quux")],
                        property: Some(Segment::Exact("rust")),
                    },
                    metadata: None,
                },
                Selector {
                    component: ComponentSelector {
                        segments: vec![Segment::Exact("core"), Segment::Exact("foo"),],
                    },
                    tree: TreeSelector {
                        node: vec![Segment::Exact("bar baz")],
                        property: Some(Segment::Exact("quux"))
                    },
                    metadata: Some(MetadataSelector::new(vec![FilterExpression {
                        identifier: Identifier::Severity,
                        op: Operation::Inclusion(
                            InclusionOperator::In,
                            vec![Value::Severity(Severity::Info), Value::Severity(Severity::Error),]
                        ),
                    }])),
                },
            ]
        );
    }

    macro_rules! test_sym_parser {
        (
            parser: $parser:ident,
            accepts: [$([$($accepted:literal),+] => $expected:expr),+]
        ) => {{
            // Verify we can parse all accepted values.
            $($(assert_eq!(Ok(("", $expected)), $parser($accepted));)+)+
            // Verify we reject all rejected values.
            assert!($parser("").is_err());

            // Verify we fail to parse any of the accepted values with random case swaps.
            let accepted_values = vec![$($($accepted),+),+];
            // Generates random numbers in [0,2)
            let uniform = rand::distributions::Uniform::from(0..2);
            let mut rng = rand::thread_rng();
            for value in &accepted_values {
                loop {
                    let mut rejected_value = String::new();
                    rejected_value.reserve(value.len());

                    for byte in value.as_bytes() {
                        let c = *byte as char;
                        if uniform.sample(&mut rng) == 1 {
                            if c.is_lowercase() {
                                rejected_value.push(c.to_uppercase().next().unwrap());
                            }
                            if c.is_uppercase() {
                                rejected_value.push(c.to_lowercase().next().unwrap());
                            }
                        } else {
                            rejected_value.push(c);
                        }
                    }
                    if accepted_values.contains(&&*rejected_value) {
                        continue;
                    }
                    assert!(
                        $parser(&rejected_value).is_err(),
                        "{} should be rejected by {}", rejected_value, stringify!($operator));
                    break;
                }
            }
        }};
    }

    #[test]
    fn parse_identifier() {
        test_sym_parser! {
            parser: identifier,
            accepts: [
                [ "filename", "Filename", "FILENAME" ] => Identifier::Filename,
                [
                    "lifecycle_event_type", "LifecycleEventType", "LIFECYCLE_EVENT_TYPE",
                    "lifecycleEventType"
                ] => Identifier::LifecycleEventType,
                [
                    "line_number", "LineNumber", "LINE_NUMBER", "lineNumber"
                ] => Identifier::LineNumber,
                [ "pid", "Pid", "PID" ] => Identifier::Pid,
                [ "severity", "Severity", "SEVERITY" ] => Identifier::Severity,
                [ "tags", "Tags", "TAGS" ] => Identifier::Tags,
                [ "tid", "Tid", "TID" ] => Identifier::Tid,
                [ "timestamp", "Timestamp", "TIMESTAMP" ] => Identifier::Timestamp
            ]
        }
    }

    #[test]
    fn parse_operator() {
        test_sym_parser! {
            parser: operator,
            accepts: [
                [ "=" ] => Operator::Comparison(ComparisonOperator::Equal),
                [ ">" ] => Operator::Comparison(ComparisonOperator::Greater),
                [ ">=" ] => Operator::Comparison(ComparisonOperator::GreaterEq),
                [ "<" ] => Operator::Comparison(ComparisonOperator::Less),
                [ "<=" ] => Operator::Comparison(ComparisonOperator::LessEq),
                [ "!=" ] => Operator::Comparison(ComparisonOperator::NotEq),
                [ "in", "IN" ] => Operator::Inclusion(InclusionOperator::In),
                [ "has any", "HAS ANY" ] => Operator::Inclusion(InclusionOperator::HasAny),
                [ "has all", "HAS ALL" ] => Operator::Inclusion(InclusionOperator::HasAll)
            ]
        }
    }

    #[test]
    fn parse_severity() {
        test_sym_parser! {
            parser: severity_sym,
            accepts: [
                [ "trace", "TRACE", "Trace" ] => Severity::Trace,
                [ "debug", "DEBUG", "Debug" ] => Severity::Debug,
                [ "info", "INFO", "Info" ] => Severity::Info,
                [ "warn", "WARN", "Warn" ] => Severity::Warn,
                [ "error", "ERROR", "Error" ] => Severity::Error
            ]
        }
    }

    #[test]
    fn parse_string_literal() {
        // Only strings within quotes are accepted
        assert_eq!(Ok(("", "foo")), string_literal("\"foo\""));
        assert_eq!(Ok(("", "_2")), string_literal("\"_2\""));
        assert_eq!(Ok(("", "$3.x")), string_literal("\"$3.x\""));

        // The empty string is a valid string
        assert_eq!(Ok(("", "")), string_literal("\"\""));

        // Inputs with missing quotes aren't accepted.
        assert!(string_literal("\"foo").is_err());
        assert!(string_literal("foo\"").is_err());
        assert!(string_literal("foo").is_err());
    }

    #[test]
    fn parse_number() {
        // Unsigned 64 bit integers are accepted.
        assert_eq!(Ok(("", 0)), number("0"));
        assert_eq!(Ok(("", 1234567890)), number("1234567890"));
        assert_eq!(Ok(("", std::u64::MAX)), number(&format!("{}", std::u64::MAX)));

        // Unsigned hexadecimal 64 bit integers are accepted.
        assert_eq!(Ok(("", 0)), number("0x0"));
        assert_eq!(Ok(("", 1311768467463790320)), number("0x123456789abcdef0"));
        assert_eq!(Ok(("", std::u64::MAX)), number("0xffffffffffffffff"));

        // Not hexadecimal chars are rejected
        assert_eq!(Ok(("g", 171)), number("0xabg"));

        // Negative numbers aren't accepted, for now.
        assert!(number("-1").is_err());
        assert!(number("-0xdf").is_err());

        // Numbers that don't fit in 64 bits are rejected.
        assert!(number("18446744073709551616").is_err()); //2^64
        assert!(all_consuming(number)("0xffffffffffffffffff").is_err());
    }

    #[test]
    fn parse_list_of_values() {
        // Accepts values of the same type.
        let expected = vec![0, 25, 149].into_iter().map(|n| Value::Number(n)).collect();
        assert_eq!(Ok(("", expected)), list_of_values("[0, 25, 0x95]"));
        assert_eq!(Ok(("", vec![Value::Number(3)])), list_of_values("[3]"));

        let expected =
            vec![Severity::Info, Severity::Warn].into_iter().map(|s| Value::Severity(s)).collect();
        assert_eq!(Ok(("", expected)), list_of_values("[INFO,warn]"));

        let expected = vec!["foo", "bar"].into_iter().map(|s| Value::StringLiteral(s)).collect();
        assert_eq!(Ok(("", expected)), list_of_values(r#"[ "foo", "bar" ]"#));

        // Rejects values of mixed types
        assert!(list_of_values("[INFO, 2]").is_err());
        assert!(list_of_values(r#"[1, "foo"]"#).is_err());
        assert!(list_of_values(r#"["bar", WARN]"#).is_err());
        assert!(list_of_values(r#"["bar", 2, error]"#).is_err());

        // The empty list is rejected.
        assert!(list_of_values("[]").is_err());
    }

    #[test]
    fn parse_filter_expression() {
        let expected = FilterExpression {
            identifier: Identifier::Pid,
            op: Operation::Comparison(ComparisonOperator::Equal, Value::Number(123)),
        };
        assert_eq!(Ok(("", expected)), filter_expression("pid = 123"));

        let expected = FilterExpression {
            identifier: Identifier::Severity,
            op: Operation::Comparison(
                ComparisonOperator::GreaterEq,
                Value::Severity(Severity::Info),
            ),
        };
        assert_eq!(Ok(("", expected)), filter_expression("severity>=info"));

        // All three operands are required
        assert!(filter_expression("tid >").is_err());
        assert!(filter_expression("!= 3").is_err());

        // The inclusion operator HAS can be used with lists and single values.
        let expected = FilterExpression {
            identifier: Identifier::Tags,
            op: Operation::Inclusion(
                InclusionOperator::HasAny,
                vec![Value::StringLiteral("foo"), Value::StringLiteral("bar")],
            ),
        };
        assert_eq!(Ok(("", expected)), filter_expression("tags HAS ANY [\"foo\", \"bar\"]"));

        let expected = FilterExpression {
            identifier: Identifier::Tags,
            op: Operation::Inclusion(InclusionOperator::HasAny, vec![Value::StringLiteral("foo")]),
        };
        assert_eq!(Ok(("", expected)), filter_expression("tags has any \"foo\""));

        // The inclusion operator IN can only be used with lists.
        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            op: Operation::Inclusion(
                InclusionOperator::In,
                vec![Value::StringLiteral("started"), Value::StringLiteral("stopped")],
            ),
        };
        assert_eq!(
            Ok(("", expected)),
            filter_expression("lifecycle_event_type in [\"started\", \"stopped\"]")
        );
        assert!(filter_expression("pid in 123").is_err());
    }

    #[test]
    fn filename_operations() {
        let expected = FilterExpression {
            identifier: Identifier::Filename,
            op: Operation::Inclusion(InclusionOperator::In, vec![Value::StringLiteral("foo.rs")]),
        };
        assert_eq!(Ok(("", expected)), filter_expression("filename in [\"foo.rs\"]"));

        let expected = FilterExpression {
            identifier: Identifier::Filename,
            op: Operation::Comparison(ComparisonOperator::Equal, Value::StringLiteral("foo.rs")),
        };
        assert_eq!(Ok(("", expected)), filter_expression("filename = \"foo.rs\""));

        let expected = FilterExpression {
            identifier: Identifier::Filename,
            op: Operation::Comparison(ComparisonOperator::NotEq, Value::StringLiteral("foo.rs")),
        };
        assert_eq!(Ok(("", expected)), filter_expression("filename != \"foo.rs\""));

        assert!(filter_expression("filename > \"foo.rs\"").is_err());
        assert!(filter_expression("filename < \"foo.rs\"").is_err());
        assert!(filter_expression("filename >= \"foo.rs\"").is_err());
        assert!(filter_expression("filename <= \"foo.rs\"").is_err());
        assert!(filter_expression("filename has any [\"foo.rs\"]").is_err());
        assert!(filter_expression("filename has all [\"foo.rs\"]").is_err());
    }

    #[test]
    fn lifecycle_event_type_operations() {
        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            op: Operation::Inclusion(InclusionOperator::In, vec![Value::StringLiteral("stopped")]),
        };
        assert_eq!(Ok(("", expected)), filter_expression("lifecycle_event_type in [\"stopped\"]"));

        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            op: Operation::Comparison(ComparisonOperator::Equal, Value::StringLiteral("stopped")),
        };
        assert_eq!(Ok(("", expected)), filter_expression("lifecycle_event_type = \"stopped\""));

        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            op: Operation::Comparison(ComparisonOperator::NotEq, Value::StringLiteral("stopped")),
        };
        assert_eq!(Ok(("", expected)), filter_expression("lifecycle_event_type != \"stopped\""));

        assert!(filter_expression("lifecycle_event_type > \"stopped\"").is_err());
        assert!(filter_expression("lifecycle_event_type < \"started\"").is_err());
        assert!(filter_expression("lifecycle_event_type >= \"diagnostics_ready\"").is_err());
        assert!(filter_expression("lifecycle_event_type <= \"log_sink_connected\"").is_err());
        assert!(
            filter_expression("lifecycle_event_type has all [\"started\", \"stopped\"]").is_err()
        );
        assert!(
            filter_expression("lifecycle_event_type has any [\"started\", \"stopped\"]").is_err()
        );
    }

    #[test]
    fn line_number_pid_tid_operations() {
        for (identifier, identifier_str) in vec![
            (Identifier::Pid, "pid"),
            (Identifier::Tid, "tid"),
            (Identifier::LineNumber, "line_number"),
        ] {
            let expected = FilterExpression {
                identifier: identifier.clone(),
                op: Operation::Inclusion(
                    InclusionOperator::In,
                    vec![Value::Number(100), Value::Number(200)],
                ),
            };
            assert_eq!(
                Ok(("", expected)),
                filter_expression(&format!("{} in [100, 200]", identifier_str))
            );

            let expected = FilterExpression {
                identifier: identifier.clone(),
                op: Operation::Comparison(ComparisonOperator::Equal, Value::Number(123)),
            };
            assert_eq!(Ok(("", expected)), filter_expression(&format!("{} = 123", identifier_str)));

            let expected = FilterExpression {
                identifier: identifier.clone(),
                op: Operation::Comparison(ComparisonOperator::NotEq, Value::Number(123)),
            };
            assert_eq!(
                Ok(("", expected)),
                filter_expression(&format!("{} != 123", identifier_str))
            );

            assert!(filter_expression(&format!("{} > 1", identifier_str)).is_err());
            assert!(filter_expression(&format!("{} < 2", identifier_str)).is_err());
            assert!(filter_expression(&format!("{} >= 3", identifier_str)).is_err());
            assert!(filter_expression(&format!("{} <= 4", identifier_str)).is_err());
            assert!(filter_expression(&format!("{} has any [5, 6]", identifier_str)).is_err());
            assert!(filter_expression(&format!("{} has all [5, 6]", identifier_str)).is_err());
        }
    }

    #[test]
    fn tags_operations() {
        for (operator, operator_str) in
            vec![(InclusionOperator::HasAny, "has any"), (InclusionOperator::HasAll, "has all")]
        {
            let expected = FilterExpression {
                identifier: Identifier::Tags,
                op: Operation::Inclusion(
                    operator,
                    vec![Value::StringLiteral("a"), Value::StringLiteral("b")],
                ),
            };
            assert_eq!(
                Ok(("", expected)),
                filter_expression(&format!("tags {} [\"a\", \"b\"]", operator_str))
            );
        }

        assert!(filter_expression("tags > \"a\"").is_err());
        assert!(filter_expression("tags < \"b\"").is_err());
        assert!(filter_expression("tags >= \"c\"").is_err());
        assert!(filter_expression("tags <= \"d\"").is_err());
        assert!(filter_expression("tags = \"e\"").is_err());
        assert!(filter_expression("tags != \"f\"").is_err());
        assert!(filter_expression("tags in [\"g\", \"h\"]").is_err());
    }

    #[test]
    fn timestamp_operations() {
        for (operator, operator_str) in vec![
            (ComparisonOperator::Equal, "="),
            (ComparisonOperator::GreaterEq, ">="),
            (ComparisonOperator::Greater, ">"),
            (ComparisonOperator::LessEq, "<="),
            (ComparisonOperator::Less, "<"),
            (ComparisonOperator::NotEq, "!="),
        ] {
            let expected = FilterExpression {
                identifier: Identifier::Timestamp,
                op: Operation::Comparison(operator, Value::Number(123)),
            };
            assert_eq!(
                Ok(("", expected)),
                filter_expression(&format!("timestamp {} 123", operator_str))
            );
        }
        assert!(filter_expression("timestamp in [1, 2]").is_err());
        assert!(filter_expression("timestamp has any [3, 4]").is_err());
        assert!(filter_expression("timestamp has all [5, 6]").is_err());
    }

    #[test]
    fn severity_operations() {
        for (operator, operator_str) in vec![
            (ComparisonOperator::Equal, "="),
            (ComparisonOperator::GreaterEq, ">="),
            (ComparisonOperator::Greater, ">"),
            (ComparisonOperator::LessEq, "<="),
            (ComparisonOperator::Less, "<"),
            (ComparisonOperator::NotEq, "!="),
        ] {
            let expected = FilterExpression {
                identifier: Identifier::Severity,
                op: Operation::Comparison(operator, Value::Severity(Severity::Info)),
            };
            assert_eq!(
                Ok(("", expected)),
                filter_expression(&format!("severity {} info", operator_str))
            );
        }

        let expected = FilterExpression {
            identifier: Identifier::Severity,
            op: Operation::Inclusion(
                InclusionOperator::In,
                vec![Value::Severity(Severity::Info), Value::Severity(Severity::Error)],
            ),
        };
        assert_eq!(Ok(("", expected)), filter_expression("severity in [info, error]"));

        assert!(filter_expression("severity has any [info, error]").is_err());
        assert!(filter_expression("severity has all [warn]").is_err());
    }

    #[test]
    fn allowed_severity_types() {
        let expected = FilterExpression {
            identifier: Identifier::Severity,
            op: Operation::Comparison(ComparisonOperator::Equal, Value::Severity(Severity::Info)),
        };
        assert_eq!(Ok(("", expected)), filter_expression("severity = info"));
        assert!(filter_expression("severity = 2").is_err());
        assert!(filter_expression("severity = \"info\"").is_err());
    }

    #[test]
    fn allowed_numeric_identifiers() {
        for (identifier, name) in vec![
            (Identifier::Pid, "pid"),
            (Identifier::Tid, "tid"),
            (Identifier::LineNumber, "line_number"),
            (Identifier::Timestamp, "timestamp"),
        ] {
            let expected = FilterExpression {
                identifier,
                op: Operation::Comparison(ComparisonOperator::Equal, Value::Number(42)),
            };
            assert_eq!(Ok(("", expected)), filter_expression(&format!("{} = 42", name)));
            assert!(filter_expression(&format!("{} = info", name)).is_err());
            assert!(filter_expression(&format!("{} = \"42\"", name)).is_err());
        }
    }

    #[test]
    fn allowed_string_identifiers() {
        for (identifier, name) in vec![
            (Identifier::Filename, "filename"),
            (Identifier::LifecycleEventType, "lifecycle_event_type"),
        ] {
            let expected = FilterExpression {
                identifier,
                op: Operation::Comparison(ComparisonOperator::Equal, Value::StringLiteral("foo")),
            };
            assert_eq!(Ok(("", expected)), filter_expression(&format!("{} = \"foo\"", name)));
            assert!(filter_expression(&format!("{} = info", name)).is_err());
            assert!(filter_expression(&format!("{} = 42", name)).is_err());
        }

        let expected = FilterExpression {
            identifier: Identifier::Tags,
            op: Operation::Inclusion(
                InclusionOperator::HasAny,
                vec![Value::StringLiteral("a"), Value::StringLiteral("b")],
            ),
        };
        assert_eq!(Ok(("", expected)), filter_expression("tags has any [\"a\", \"b\"]"));
        assert!(filter_expression("tags has any [info, error]").is_err());
        assert!(filter_expression("tags has any [2, 3]").is_err());
    }

    #[test]
    fn parse_metadata_selector() {
        let expected = MetadataSelector::new(vec![
            FilterExpression {
                identifier: Identifier::LineNumber,
                op: Operation::Comparison(ComparisonOperator::Equal, Value::Number(10)),
            },
            FilterExpression {
                identifier: Identifier::Filename,
                op: Operation::Inclusion(
                    InclusionOperator::In,
                    vec![Value::StringLiteral("foo.rs")],
                ),
            },
            FilterExpression {
                identifier: Identifier::Severity,
                op: Operation::Comparison(
                    ComparisonOperator::LessEq,
                    Value::Severity(Severity::Error),
                ),
            },
            FilterExpression {
                identifier: Identifier::Tags,
                op: Operation::Inclusion(
                    InclusionOperator::HasAll,
                    vec![Value::StringLiteral("foo"), Value::StringLiteral("bar")],
                ),
            },
        ]);
        assert_eq!(
            Ok(("", expected)),
            metadata_selector(
                "where line_number = 10, filename in [\"foo.rs\"],\
                 severity <= ERROR, tags HAS ALL [\"foo\", \"bar\"]"
            )
        );

        // Requires >= 1 filters.
        let expected = MetadataSelector::new(vec![FilterExpression {
            identifier: Identifier::Timestamp,
            op: Operation::Comparison(ComparisonOperator::Greater, Value::Number(123)),
        }]);
        assert_eq!(Ok(("", expected)), metadata_selector("WHERE timestamp > 123"));
        assert!(metadata_selector("where").is_err());
    }
}
