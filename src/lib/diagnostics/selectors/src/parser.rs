// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/55118): remove.
#![allow(dead_code)]

use nom::{
    branch::alt,
    bytes::complete::{is_not, tag},
    character::complete::{char, digit1, hex_digit1, multispace0},
    combinator::{map, recognize},
    error::{make_error, ErrorKind, ParseError},
    multi::{many0, separated_nonempty_list},
    sequence::{delimited, preceded},
    IResult,
};

/// This macro generates:
/// - The `enum Operator` containing the given `$variant_name`s as the variants.
/// - A parser function `$name` that consumes an input of value `$symbol` and returns an
///   `Operator::$variant_name`.
/// - A function `operator` that parses an input into an `Operator`.
macro_rules! operator {
    ($($symbol:literal => $variant_name:ident => $name:ident),+) => {
        #[derive(Debug, Eq, PartialEq)]
        pub enum Operator {
            $($variant_name),+
        }

        $(
            fn $name(input: &str) -> IResult<&str, Operator> {
                map(tag($symbol), move |_| Operator::$variant_name)(input)
            }
        )+

        fn operator(input: &str) -> IResult<&str, Operator> {
            alt(($( $name ),+))(input)
        }
    };
}

operator!(
   "=" => Equal => equal,
   ">=" => GreaterEq => greater_eq,
   ">" => Greater => greater,
   "<=" => LessEq => less_eq,
   "<" => Less => less,
   "!=" => NotEq => not_eq
);
//Maybe include IN, HAS above.

/// This macro generates:
/// - The `enum $ty` containing the given `$variant_name`s as the variants.
/// - A general parser function `$parser` for all the given `tags`.
/// - Parser functions `$tag_parser` that parses inputs that match the given accepted `$tag`s into
///   `Severity`.
/// - A function `severity_sym` that parses an input into a `Severity`.
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
        #[derive(Debug, Eq, PartialEq)]
        pub enum $type {
            $($variant_name),+
        }

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

/// Parses an input containing whitespace at the front.
fn spaced<'a, E, F, O>(parser: F) -> impl Fn(&'a str) -> IResult<&'a str, O, E>
where
    F: Fn(&'a str) -> IResult<&'a str, O, E>,
    E: ParseError<&'a str>,
{
    preceded(multispace0, parser)
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
    let (rest, value) = digit1(input)?;
    let number = value
        .parse::<u64>()
        .map_err(|_err| nom::Err::Failure(make_error(input, ErrorKind::TooLarge)))?;
    Ok((rest, number))
}

/// Parses a hexadecimal number as 64 bit integer.
fn hex_integer(input: &str) -> IResult<&str, u64> {
    let (rest, value) = preceded(tag("0x"), hex_digit1)(input)?;
    let number = u64::from_str_radix(&value, 16)
        .map_err(|_err| nom::Err::Failure(make_error(input, ErrorKind::TooLarge)))?;
    Ok((rest, number))
}

/// Parses a unsigned decimal and hexadecimal numbers of 64 bits.
/// For example: 123, 0x7b will be accepted as
// TODO(fxbug.dev/55118): consider also accepting signed numbers.
fn number(input: &str) -> IResult<&str, u64> {
    alt((hex_integer, integer))(input)
}

#[derive(Debug, Eq, PartialEq)]
pub enum Value<'a> {
    Severity(Severity),
    StringLiteral(&'a str),
    Number(u64),
}

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

#[derive(Debug, Eq, PartialEq)]
pub enum Operation<'a> {
    Comparison(Operator, Value<'a>),
    // TODO(fxbug.dev/55118): HasOne(Value),
    // TODO(fxbug.dev/55118): HasAny(Vec<Value>),
    // TODO(fxbug.dev/55118): In(Vec<Value>),
}

#[derive(Debug, Eq, PartialEq)]
pub struct FilterExpression<'a> {
    pub identifier: Identifier,
    pub op: Operation<'a>,
}

fn filter_expression(input: &str) -> IResult<&str, FilterExpression<'_>> {
    // TODO(fxbug.dev/55118): validate that identifiers are used with valid operators. Consider if
    // this has to be done at parse time or we can defer it for a follow-up validation step.
    let (rest, identifier) = spaced(identifier)(input)?;
    let (rest, operator) = spaced(operator)(rest)?;
    // TODO(fxbug.dev/55118): support the rest of the operations (In, Has).
    let (rest, value) = spaced(alt((
        map(number, Value::Number),
        map(severity_sym, |s| Value::Severity(s)),
        map(string_literal, |s| Value::StringLiteral(s)),
    )))(rest)?;
    Ok((rest, FilterExpression { identifier, op: Operation::Comparison(operator, value) }))
}

#[derive(Debug, Eq, PartialEq)]
pub struct MetadataSelector<'a>(Vec<FilterExpression<'a>>);

impl<'a> MetadataSelector<'a> {
    fn filters(&self) -> &Vec<FilterExpression<'a>> {
        &self.0
    }
}

/// Parses a metadata selector
fn metadata_selector(input: &str) -> IResult<&str, MetadataSelector<'_>> {
    let (rest, _) = spaced(alt((tag("WHERE"), tag("where"))))(input)?;
    let (rest, filters) = spaced(separated_nonempty_list(char(','), filter_expression))(rest)?;
    Ok((rest, MetadataSelector(filters)))
}

// TODO(fxbug.dev/55118): support time diferences (1h30m, 30s, etc)
// TODO(fxbug.dev/55118): implement parsing of component and tree selectors using nom.
///// Parses the input into a `Selector`.
//pub fn selector(input: &str) -> nom::IResult<&str, Selector> {
//    let (rest, component_selector) = component_selector(input);
//    let (rest, tree_selector) = tree_selector(rest);
//    let (rest, metadata_selector) = metadata_selector(rest);
//    Ok(Selector { component_selector, tree_selector, metadata_selector }
//}

#[cfg(test)]
mod tests {
    use super::*;
    use rand::distributions::Distribution;

    macro_rules! test_sym_parser {
        (
            parser: $parser:ident,
            accepts: [$([$($accepted:literal),+] => $expected:expr),+],
            rejects: [$($rejected:literal),*]
        ) => {{
            // Verify we can parse all accepted values.
            $($(assert_eq!(Ok(("", $expected)), $parser($accepted));)+)+
            // Verify we reject all rejected values.
            $(assert!($parser($rejected).is_err());)*
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
                    assert!($parser(&rejected_value).is_err());
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
                    "lifecycle_event_type", "LifecycleEventType", "LIFECYCLE_EVENT_TYPE"
                ] => Identifier::LifecycleEventType,
                [ "line_number", "LineNumber", "LINE_NUMBER" ] => Identifier::LineNumber,
                [ "pid", "Pid", "PID" ] => Identifier::Pid,
                [ "severity", "Severity", "SEVERITY" ] => Identifier::Severity,
                [ "tags", "Tags", "TAGS" ] => Identifier::Tags,
                [ "tid", "Tid", "TID" ] => Identifier::Tid,
                [ "timestamp", "Timestamp", "TIMESTAMP" ] => Identifier::Timestamp
            ],
            rejects: []
        }
    }

    #[test]
    fn parse_operator() {
        test_sym_parser! {
            parser: operator,
            accepts: [
                [ "=" ] => Operator::Equal,
                [ ">" ] => Operator::Greater,
                [ ">=" ] => Operator::GreaterEq,
                [ "<" ] => Operator::Less,
                [ "<=" ] => Operator::LessEq,
                [ "!=" ] => Operator::NotEq
            ],
            rejects: [ "in", "IN", "In", "has", "Has", "HAS" ]
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
            ],
            rejects: []
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
    fn prase_number() {
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
        assert!(number("0xffffffffffffffffff").is_err());
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
            op: Operation::Comparison(Operator::Equal, Value::Number(123)),
        };
        assert_eq!(Ok(("", expected)), filter_expression("pid = 123"));

        let expected = FilterExpression {
            identifier: Identifier::Severity,
            op: Operation::Comparison(Operator::GreaterEq, Value::Severity(Severity::Info)),
        };
        assert_eq!(Ok(("", expected)), filter_expression("severity>=info"));

        // All three operands are required
        assert!(filter_expression("tid >").is_err());
        assert!(filter_expression("!= 3").is_err());
    }

    #[test]
    fn parse_metadata_selector() {
        let expected = MetadataSelector(vec![
            FilterExpression {
                identifier: Identifier::LineNumber,
                op: Operation::Comparison(Operator::Equal, Value::Number(10)),
            },
            FilterExpression {
                identifier: Identifier::Filename,
                op: Operation::Comparison(Operator::Equal, Value::StringLiteral("foo.rs")),
            },
            FilterExpression {
                identifier: Identifier::Severity,
                op: Operation::Comparison(Operator::LessEq, Value::Severity(Severity::Error)),
            },
        ]);
        assert_eq!(
            Ok(("", expected)),
            metadata_selector("where line_number = 10, filename = \"foo.rs\", severity <= ERROR")
        );

        // Requires >= 1 filters.
        let expected = MetadataSelector(vec![FilterExpression {
            identifier: Identifier::Timestamp,
            op: Operation::Comparison(Operator::Greater, Value::Number(123)),
        }]);
        assert_eq!(Ok(("", expected)), metadata_selector("WHERE timestamp > 123"));
        assert!(metadata_selector("where").is_err());
    }
}
