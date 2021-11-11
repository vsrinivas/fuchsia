// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    error::Error,
    types::*,
    validate::{ValidateComponentSelectorExt, ValidateExt},
};
use nom::{
    self,
    branch::alt,
    bytes::complete::{escaped, is_not, tag, take_while, take_while1},
    character::complete::{alphanumeric1, char, digit1, hex_digit1, multispace0, none_of, one_of},
    combinator::{all_consuming, complete, cond, map, map_res, opt, peek, recognize, verify},
    error::{ErrorKind, ParseError},
    multi::{many0, separated_nonempty_list},
    sequence::{delimited, pair, preceded, tuple},
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
    // TODO(fxbug.dev/86963): this doesn't handle escape sequences.
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
// TODO(fxbug.dev/86964): consider also accepting signed numbers.
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
    let (rest, operator) = spaced(operator)(rest)?;
    let (rest, value) =
        // TODO(fxbug.dev/86960): similar to severities, we can probably have reserved values
        // for lifecycle event types.
        // TODO(fxbug.dev/86961): support time diferences (1h30m, 30s, etc) instead of only
        // timestamp comparison.
        spaced(alt((
            map(number, move |n| OneOrMany::One(Value::Number(n))),
            map(severity_sym, move |s| OneOrMany::One(Value::Severity(s))),
            map(string_literal, move |s| OneOrMany::One(Value::StringLiteral(s))),
            map(list_of_values, OneOrMany::Many),
        )))(rest)?;
    Ok((rest, FilterExpression { identifier, operator, value }))
}

/// Parses a metadata selector.
fn metadata_selector(input: &str) -> IResult<&str, MetadataSelector<'_>> {
    let (rest, _) = spaced(alt((tag("WHERE"), tag("where"))))(input)?;
    let (rest, filters) = spaced(separated_nonempty_list(char(','), filter_expression))(rest)?;
    Ok((rest, MetadataSelector::new(filters)))
}

/// Parses a tree selector, which is a node selector and an optional property selector.
fn tree_selector(input: &str) -> IResult<&str, TreeSelector<'_>> {
    let esc = escaped(none_of(":/\\ \t\n"), '\\', one_of("* \t/:\\"));
    let (rest, node_segments) = separated_nonempty_list(tag("/"), &esc)(input)?;
    let (rest, property_segment) = if peek::<&str, _, (&str, ErrorKind), _>(tag(":"))(rest).is_ok()
    {
        let (rest, _) = tag(":")(rest)?;
        let (rest, property) = verify(esc, |value: &str| !value.is_empty())(rest)?;
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

/// Parses a component selector.
fn component_selector(input: &str) -> IResult<&str, ComponentSelector<'_>> {
    let accepted_characters = escaped(
        alt((alphanumeric1, tag("*"), tag("."), tag("-"), tag("_"), tag(">"), tag("<"))),
        '\\',
        tag(":"),
    );
    let (rest, segments) =
        separated_nonempty_list(tag("/"), recognize(accepted_characters))(input)?;
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
    let (rest, (component, _, tree)) = tuple((component_selector, tag(":"), tree_selector))(input)?;
    Ok((rest, (component, tree)))
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
pub fn selector(input: &str) -> Result<Selector<'_>, Error> {
    let result = complete(all_consuming(do_parse_selector(/*allow_inline_comment=*/ false)))(input);
    match result {
        Ok((_, selector)) => {
            selector.validate()?;
            Ok(selector)
        }
        Err(nom::Err::Error(e) | nom::Err::Failure(e)) => Err(e.into()),
        _ => unreachable!("through the complete combinator we get rid of Incomplete"),
    }
}

/// Parses the input into a `ComponentSelector` ignoring any whitespace around the component
/// selector.
pub fn consuming_component_selector(input: &str) -> Result<ComponentSelector<'_>, Error> {
    let result =
        nom::combinator::all_consuming(pair(spaced(component_selector), multispace0))(input);
    match result {
        Ok((_, (component_selector, _))) => {
            component_selector.validate()?;
            Ok(component_selector)
        }
        Err(nom::Err::Error(e) | nom::Err::Failure(e)) => Err(e.into()),
        _ => unreachable!("through the complete combinator we get rid of Incomplete"),
    }
}

/// Parses the given input line into a Selector or None.
pub fn selector_or_comment(input: &str) -> Result<Option<Selector<'_>>, Error> {
    let result = complete(all_consuming(alt((
        map(comment, |_| None),
        map(do_parse_selector(/*allow_inline_comment=*/ true), |s| Some(s)),
    ))))(input);
    match result {
        Ok((_, maybe_selector)) => match maybe_selector {
            None => Ok(None),
            Some(selector) => {
                selector.validate()?;
                Ok(Some(selector))
            }
        },
        Err(nom::Err::Error(e) | nom::Err::Failure(e)) => Err(e.into()),
        _ => unreachable!("through the complete combinator we get rid of Incomplete"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nom::combinator::all_consuming;
    use rand::distributions::Distribution;

    #[fuchsia::test]
    fn canonical_component_selector_test() {
        let test_vector = vec![
            (
                "a/b/c",
                vec![
                    Segment::ExactMatch("a".into()),
                    Segment::ExactMatch("b".into()),
                    Segment::ExactMatch("c".into()),
                ],
            ),
            (
                "a/*/c",
                vec![
                    Segment::ExactMatch("a".into()),
                    Segment::Pattern("*"),
                    Segment::ExactMatch("c".into()),
                ],
            ),
            (
                "a/b*/c",
                vec![
                    Segment::ExactMatch("a".into()),
                    Segment::Pattern("b*"),
                    Segment::ExactMatch("c".into()),
                ],
            ),
            (
                "a/b/**",
                vec![
                    Segment::ExactMatch("a".into()),
                    Segment::ExactMatch("b".into()),
                    Segment::Pattern("**"),
                ],
            ),
            (
                "core/session\\:id/foo",
                vec![
                    Segment::ExactMatch("core".into()),
                    Segment::ExactMatch("session:id".into()),
                    Segment::ExactMatch("foo".into()),
                ],
            ),
            ("c", vec![Segment::ExactMatch("c".into())]),
            ("<component_manager>", vec![Segment::ExactMatch("<component_manager>".into())]),
            (
                r#"a/*/b/**"#,
                vec![
                    Segment::ExactMatch("a".into()),
                    Segment::Pattern("*"),
                    Segment::ExactMatch("b".into()),
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

    #[fuchsia::test]
    fn missing_path_component_selector_test() {
        let component_selector_string = "c";
        let (_, component_selector) = component_selector(component_selector_string).unwrap();
        let mut path_vec = component_selector.segments;
        assert_eq!(path_vec.pop(), Some(Segment::ExactMatch("c".into())));
        assert!(path_vec.is_empty());
    }

    #[fuchsia::test]
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
            let component_selector_result = consuming_component_selector(test_string);
            assert!(component_selector_result.is_err(), "expected '{}' to fail", test_string);
        }
    }

    #[fuchsia::test]
    fn canonical_tree_selector_test() {
        let test_vector = vec![
            (
                "a/b:c",
                vec![Segment::ExactMatch("a".into()), Segment::ExactMatch("b".into())],
                Some(Segment::ExactMatch("c".into())),
            ),
            (
                "a/*:c",
                vec![Segment::ExactMatch("a".into()), Segment::Pattern("*")],
                Some(Segment::ExactMatch("c".into())),
            ),
            (
                "a/b:*",
                vec![Segment::ExactMatch("a".into()), Segment::ExactMatch("b".into())],
                Some(Segment::Pattern("*")),
            ),
            ("a/b", vec![Segment::ExactMatch("a".into()), Segment::ExactMatch("b".into())], None),
            (
                r#"a/b\:\*c"#,
                vec![Segment::ExactMatch("a".into()), Segment::ExactMatch("b:*c".into())],
                None,
            ),
        ];

        for (string, expected_path, expected_property) in test_vector {
            let (_, tree_selector) = tree_selector(string).unwrap();
            assert_eq!(
                tree_selector,
                TreeSelector { node: expected_path, property: expected_property }
            );
        }
    }

    #[fuchsia::test]
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
            // prepend a placeholder component selector so that we exercise the validation code.
            let test_selector = format!("a:{}", string);
            assert!(selector(&test_selector).is_err(), "{} should fail", test_selector);
        }
    }

    #[fuchsia::test]
    fn tree_selector_with_spaces() {
        let with_spaces = vec![
            (
                r#"a\ b:c"#,
                vec![Segment::ExactMatch("a b".into())],
                Some(Segment::ExactMatch("c".into())),
            ),
            (
                r#"ab/\ d:c\ "#,
                vec![Segment::ExactMatch("ab".into()), Segment::ExactMatch(" d".into())],
                Some(Segment::ExactMatch("c ".into())),
            ),
            ("a\\\t*b:c", vec![Segment::Pattern("a\\\t*b")], Some(Segment::ExactMatch("c".into()))),
            (
                r#"a\ "x":c"#,
                vec![Segment::ExactMatch(r#"a "x""#.into())],
                Some(Segment::ExactMatch("c".into())),
            ),
        ];
        for (string, node, property) in with_spaces {
            assert_eq!(
                all_consuming(tree_selector)(string).unwrap().1,
                TreeSelector { node, property }
            );
        }

        // Un-escaped quotes aren't accepted when parsing with spaces.
        assert!(all_consuming(tree_selector)(r#"a/b:"xc"/d"#).is_err());
    }

    #[fuchsia::test]
    fn parse_full_selector() {
        assert_eq!(
            selector(
                "core/**:some-node/he*re:prop where filename = \"baz\", severity in [info, error]"
            )
            .unwrap(),
            Selector {
                component: ComponentSelector {
                    segments: vec![Segment::ExactMatch("core".into()), Segment::Pattern("**"),],
                },
                tree: TreeSelector {
                    node: vec![Segment::ExactMatch("some-node".into()), Segment::Pattern("he*re"),],
                    property: Some(Segment::ExactMatch("prop".into())),
                },
                metadata: Some(MetadataSelector::new(vec![
                    FilterExpression {
                        identifier: Identifier::Filename,
                        operator: Operator::Comparison(ComparisonOperator::Equal),
                        value: OneOrMany::One(Value::StringLiteral("baz")),
                    },
                    FilterExpression {
                        identifier: Identifier::Severity,
                        operator: Operator::Inclusion(InclusionOperator::In),
                        value: OneOrMany::Many(vec![
                            Value::Severity(Severity::Info),
                            Value::Severity(Severity::Error)
                        ])
                    },
                ])),
            }
        );

        // Parses selectors without metadata. Also ignores whitespace.
        assert_eq!(
            selector("   foo:bar  ").unwrap(),
            Selector {
                component: ComponentSelector { segments: vec![Segment::ExactMatch("foo".into())] },
                tree: TreeSelector {
                    node: vec![Segment::ExactMatch("bar".into())],
                    property: None
                },
                metadata: None,
            }
        );

        // At least one filter is required when `where` is provided.
        assert!(selector("foo:bar where").is_err());
    }

    #[fuchsia::test]
    fn assert_no_trailing_backward_slash() {
        assert!(selector(r#"foo:bar:baz\"#).is_err());
    }

    #[fuchsia::test]
    fn parse_full_selector_with_spaces() {
        assert_eq!(
            selector(r#"core/foo:some\ node/*:prop where pid = 123"#).unwrap(),
            Selector {
                component: ComponentSelector {
                    segments: vec![
                        Segment::ExactMatch("core".into()),
                        Segment::ExactMatch("foo".into()),
                    ],
                },
                tree: TreeSelector {
                    node: vec![Segment::ExactMatch("some node".into()), Segment::Pattern("*"),],
                    property: Some(Segment::ExactMatch("prop".into())),
                },
                metadata: Some(MetadataSelector::new(vec![FilterExpression {
                    identifier: Identifier::Pid,
                    operator: Operator::Comparison(ComparisonOperator::Equal),
                    value: OneOrMany::One(Value::Number(123)),
                },])),
            }
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    macro_rules! test_filter_expr {
        ($filter:expr) => {
            selector(&format!("foo:bar where {}", $filter))
                .map(|result| result.metadata.unwrap().0.into_iter().next().unwrap())
        };
    }

    #[fuchsia::test]
    fn parse_filter_expression() {
        let expected = FilterExpression {
            identifier: Identifier::Pid,
            operator: Operator::Comparison(ComparisonOperator::Equal),
            value: OneOrMany::One(Value::Number(123)),
        };
        assert_eq!(expected, test_filter_expr!("pid = 123").unwrap());

        let expected = FilterExpression {
            identifier: Identifier::Severity,
            operator: Operator::Comparison(ComparisonOperator::GreaterEq),
            value: OneOrMany::One(Value::Severity(Severity::Info)),
        };
        assert_eq!(expected, test_filter_expr!("severity>=info").unwrap());

        // All three operands are required
        assert!(test_filter_expr!("tid >").is_err());
        assert!(test_filter_expr!("!= 3").is_err());

        // The inclusion operator HAS can be used with lists and single values.
        let expected = FilterExpression {
            identifier: Identifier::Tags,
            operator: Operator::Inclusion(InclusionOperator::HasAny),
            value: OneOrMany::Many(vec![Value::StringLiteral("foo"), Value::StringLiteral("bar")]),
        };
        assert_eq!(expected, test_filter_expr!("tags HAS ANY [\"foo\", \"bar\"]").unwrap());

        let expected = FilterExpression {
            identifier: Identifier::Tags,
            operator: Operator::Inclusion(InclusionOperator::HasAny),
            value: OneOrMany::One(Value::StringLiteral("foo")),
        };
        assert_eq!(expected, test_filter_expr!("tags has any \"foo\"").unwrap());

        // The inclusion operator IN can only be used with lists.
        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            operator: Operator::Inclusion(InclusionOperator::In),
            value: OneOrMany::Many(vec![
                Value::StringLiteral("started"),
                Value::StringLiteral("stopped"),
            ]),
        };
        assert_eq!(
            expected,
            test_filter_expr!("lifecycle_event_type in [\"started\", \"stopped\"]").unwrap()
        );
        assert!(test_filter_expr!("pid in 123").is_err());
    }

    #[fuchsia::test]
    fn filename_operations() {
        let expected = FilterExpression {
            identifier: Identifier::Filename,
            operator: Operator::Inclusion(InclusionOperator::In),
            value: OneOrMany::Many(vec![Value::StringLiteral("foo.rs")]),
        };
        assert_eq!(expected, test_filter_expr!("filename in [\"foo.rs\"]").unwrap());

        let expected = FilterExpression {
            identifier: Identifier::Filename,
            operator: Operator::Comparison(ComparisonOperator::Equal),
            value: OneOrMany::One(Value::StringLiteral("foo.rs")),
        };
        assert_eq!(expected, test_filter_expr!("filename = \"foo.rs\"").unwrap());

        let expected = FilterExpression {
            identifier: Identifier::Filename,
            operator: Operator::Comparison(ComparisonOperator::NotEq),
            value: OneOrMany::One(Value::StringLiteral("foo.rs")),
        };
        assert_eq!(expected, test_filter_expr!("filename != \"foo.rs\"").unwrap());

        assert!(test_filter_expr!("filename > \"foo.rs\"").is_err());
        assert!(test_filter_expr!("filename < \"foo.rs\"").is_err());
        assert!(test_filter_expr!("filename >= \"foo.rs\"").is_err());
        assert!(test_filter_expr!("filename <= \"foo.rs\"").is_err());
        assert!(test_filter_expr!("filename has any [\"foo.rs\"]").is_err());
        assert!(test_filter_expr!("filename has all [\"foo.rs\"]").is_err());
    }

    #[fuchsia::test]
    fn lifecycle_event_type_operations() {
        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            operator: Operator::Inclusion(InclusionOperator::In),
            value: OneOrMany::Many(vec![Value::StringLiteral("stopped")]),
        };
        assert_eq!(expected, test_filter_expr!("lifecycle_event_type in [\"stopped\"]").unwrap());

        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            operator: Operator::Comparison(ComparisonOperator::Equal),
            value: OneOrMany::One(Value::StringLiteral("stopped")),
        };
        assert_eq!(expected, test_filter_expr!("lifecycle_event_type = \"stopped\"").unwrap());

        let expected = FilterExpression {
            identifier: Identifier::LifecycleEventType,
            operator: Operator::Comparison(ComparisonOperator::NotEq),
            value: OneOrMany::One(Value::StringLiteral("stopped")),
        };
        assert_eq!(expected, test_filter_expr!("lifecycle_event_type != \"stopped\"").unwrap());

        assert!(test_filter_expr!("lifecycle_event_type > \"stopped\"").is_err());
        assert!(test_filter_expr!("lifecycle_event_type < \"started\"").is_err());
        assert!(test_filter_expr!("lifecycle_event_type >= \"diagnostics_ready\"").is_err());
        assert!(test_filter_expr!("lifecycle_event_type <= \"log_sink_connected\"").is_err());
        assert!(
            test_filter_expr!("lifecycle_event_type has all [\"started\", \"stopped\"]").is_err()
        );
        assert!(
            test_filter_expr!("lifecycle_event_type has any [\"started\", \"stopped\"]").is_err()
        );
    }

    #[fuchsia::test]
    fn line_number_pid_tid_operations() {
        for (identifier, identifier_str) in vec![
            (Identifier::Pid, "pid"),
            (Identifier::Tid, "tid"),
            (Identifier::LineNumber, "line_number"),
        ] {
            let expected = FilterExpression {
                identifier: identifier.clone(),
                operator: Operator::Inclusion(InclusionOperator::In),
                value: OneOrMany::Many(vec![Value::Number(100), Value::Number(200)]),
            };
            assert_eq!(
                expected,
                test_filter_expr!(&format!("{} in [100, 200]", identifier_str)).unwrap()
            );

            let expected = FilterExpression {
                identifier: identifier.clone(),
                operator: Operator::Comparison(ComparisonOperator::Equal),
                value: OneOrMany::One(Value::Number(123)),
            };
            assert_eq!(expected, test_filter_expr!(&format!("{} = 123", identifier_str)).unwrap());

            let expected = FilterExpression {
                identifier: identifier.clone(),
                operator: Operator::Comparison(ComparisonOperator::NotEq),
                value: OneOrMany::One(Value::Number(123)),
            };
            assert_eq!(expected, test_filter_expr!(&format!("{} != 123", identifier_str)).unwrap());

            assert!(test_filter_expr!(&format!("{} > 1", identifier_str)).is_err());
            assert!(test_filter_expr!(&format!("{} < 2", identifier_str)).is_err());
            assert!(test_filter_expr!(&format!("{} >= 3", identifier_str)).is_err());
            assert!(test_filter_expr!(&format!("{} <= 4", identifier_str)).is_err());
            assert!(test_filter_expr!(&format!("{} has any [5, 6]", identifier_str)).is_err());
            assert!(test_filter_expr!(&format!("{} has all [5, 6]", identifier_str)).is_err());
        }
    }

    #[fuchsia::test]
    fn tags_operations() {
        for (operator, operator_str) in
            vec![(InclusionOperator::HasAny, "has any"), (InclusionOperator::HasAll, "has all")]
        {
            let expected = FilterExpression {
                identifier: Identifier::Tags,
                operator: Operator::Inclusion(operator),
                value: OneOrMany::Many(vec![Value::StringLiteral("a"), Value::StringLiteral("b")]),
            };
            assert_eq!(
                expected,
                test_filter_expr!(&format!("tags {} [\"a\", \"b\"]", operator_str)).unwrap()
            );
        }

        assert!(test_filter_expr!("tags > \"a\"").is_err());
        assert!(test_filter_expr!("tags < \"b\"").is_err());
        assert!(test_filter_expr!("tags >= \"c\"").is_err());
        assert!(test_filter_expr!("tags <= \"d\"").is_err());
        assert!(test_filter_expr!("tags = \"e\"").is_err());
        assert!(test_filter_expr!("tags != \"f\"").is_err());
        assert!(test_filter_expr!("tags in [\"g\", \"h\"]").is_err());
    }

    #[fuchsia::test]
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
                operator: Operator::Comparison(operator),
                value: OneOrMany::One(Value::Number(123)),
            };
            assert_eq!(
                expected,
                test_filter_expr!(&format!("timestamp {} 123", operator_str)).unwrap()
            );
        }
        assert!(test_filter_expr!("timestamp in [1, 2]").is_err());
        assert!(test_filter_expr!("timestamp has any [3, 4]").is_err());
        assert!(test_filter_expr!("timestamp has all [5, 6]").is_err());
    }

    #[fuchsia::test]
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
                operator: Operator::Comparison(operator),
                value: OneOrMany::One(Value::Severity(Severity::Info)),
            };
            assert_eq!(
                expected,
                test_filter_expr!(&format!("severity {} info", operator_str)).unwrap()
            );
        }

        let expected = FilterExpression {
            identifier: Identifier::Severity,
            operator: Operator::Inclusion(InclusionOperator::In),
            value: OneOrMany::Many(vec![
                Value::Severity(Severity::Info),
                Value::Severity(Severity::Error),
            ]),
        };
        assert_eq!(expected, test_filter_expr!("severity in [info, error]").unwrap());

        assert!(test_filter_expr!("severity has any [info, error]").is_err());
        assert!(test_filter_expr!("severity has all [warn]").is_err());
    }

    #[fuchsia::test]
    fn allowed_severity_types() {
        let expected = FilterExpression {
            identifier: Identifier::Severity,
            operator: Operator::Comparison(ComparisonOperator::Equal),
            value: OneOrMany::One(Value::Severity(Severity::Info)),
        };
        assert_eq!(expected, test_filter_expr!("severity = info").unwrap());
        assert!(test_filter_expr!("severity = 2").is_err());
        assert!(test_filter_expr!("severity = \"info\"").is_err());
    }

    #[fuchsia::test]
    fn allowed_numeric_identifiers() {
        for (identifier, name) in vec![
            (Identifier::Pid, "pid"),
            (Identifier::Tid, "tid"),
            (Identifier::LineNumber, "line_number"),
            (Identifier::Timestamp, "timestamp"),
        ] {
            let expected = FilterExpression {
                identifier,
                operator: Operator::Comparison(ComparisonOperator::Equal),
                value: OneOrMany::One(Value::Number(42)),
            };
            assert_eq!(expected, test_filter_expr!(&format!("{} = 42", name)).unwrap());
            assert!(test_filter_expr!(&format!("{} = info", name)).is_err());
            assert!(test_filter_expr!(&format!("{} = \"42\"", name)).is_err());
        }
    }

    #[fuchsia::test]
    fn allowed_string_identifiers() {
        for (identifier, name) in vec![
            (Identifier::Filename, "filename"),
            (Identifier::LifecycleEventType, "lifecycle_event_type"),
        ] {
            let expected = FilterExpression {
                identifier,
                operator: Operator::Comparison(ComparisonOperator::Equal),
                value: OneOrMany::One(Value::StringLiteral("foo")),
            };
            assert_eq!(expected, test_filter_expr!(&format!("{} = \"foo\"", name)).unwrap());
            assert!(test_filter_expr!(&format!("{} = info", name)).is_err());
            assert!(test_filter_expr!(&format!("{} = 42", name)).is_err());
        }

        let expected = FilterExpression {
            identifier: Identifier::Tags,
            operator: Operator::Inclusion(InclusionOperator::HasAny),
            value: OneOrMany::Many(vec![Value::StringLiteral("a"), Value::StringLiteral("b")]),
        };
        assert_eq!(expected, test_filter_expr!("tags has any [\"a\", \"b\"]").unwrap());
        assert!(test_filter_expr!("tags has any [info, error]").is_err());
        assert!(test_filter_expr!("tags has any [2, 3]").is_err());
    }

    #[fuchsia::test]
    fn parse_metadata_selector() {
        let expected = MetadataSelector::new(vec![
            FilterExpression {
                identifier: Identifier::LineNumber,
                operator: Operator::Comparison(ComparisonOperator::Equal),
                value: OneOrMany::One(Value::Number(10)),
            },
            FilterExpression {
                identifier: Identifier::Filename,
                operator: Operator::Inclusion(InclusionOperator::In),
                value: OneOrMany::Many(vec![Value::StringLiteral("foo.rs")]),
            },
            FilterExpression {
                identifier: Identifier::Severity,
                operator: Operator::Comparison(ComparisonOperator::LessEq),
                value: OneOrMany::One(Value::Severity(Severity::Error)),
            },
            FilterExpression {
                identifier: Identifier::Tags,
                operator: Operator::Inclusion(InclusionOperator::HasAll),
                value: OneOrMany::Many(vec![
                    Value::StringLiteral("foo"),
                    Value::StringLiteral("bar"),
                ]),
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
            operator: Operator::Comparison(ComparisonOperator::Greater),
            value: OneOrMany::One(Value::Number(123)),
        }]);
        assert_eq!(Ok(("", expected)), metadata_selector("WHERE timestamp > 123"));
        assert!(metadata_selector("where").is_err());
    }
}
