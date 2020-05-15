// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::parser_common::{
    compound_identifier, condition_value, many_until_eof, map_err, ws, BindParserError,
    CompoundIdentifier, NomSpan, Value,
};
use nom::{bytes::complete::tag, sequence::separated_pair, IResult};
use std::str::FromStr;

#[derive(Debug, Clone, PartialEq)]
pub struct DeviceSpecification {
    pub properties: Vec<Property>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Property {
    pub key: CompoundIdentifier,
    pub value: Value,
}

impl FromStr for DeviceSpecification {
    type Err = BindParserError;

    fn from_str(input: &str) -> Result<Self, Self::Err> {
        match device_specification(NomSpan::new(input)) {
            Ok((_, spec)) => Ok(spec),
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

impl DeviceSpecification {
    pub fn new() -> Self {
        DeviceSpecification { properties: Vec::new() }
    }

    pub fn add_property(&mut self, key: &str, value: &str) -> Result<(), BindParserError> {
        match property_from_pair(NomSpan::new(key), NomSpan::new(value)) {
            Ok((_, property)) => {
                self.properties.push(property);
                Ok(())
            }
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

fn property_from_pair<'a, 'b>(
    key: NomSpan<'a>,
    value: NomSpan<'b>,
) -> IResult<(NomSpan<'a>, NomSpan<'b>), Property, BindParserError> {
    let (key_remaining, key) = compound_identifier(key)?;
    if !key_remaining.fragment().is_empty() {
        return Err(nom::Err::Error(BindParserError::Eof(key_remaining.fragment().to_string())));
    }
    let (value_remaining, value) = condition_value(value)?;
    if !value_remaining.fragment().is_empty() {
        return Err(nom::Err::Error(BindParserError::Eof(value_remaining.fragment().to_string())));
    }
    Ok(((key_remaining, value_remaining), Property { key, value }))
}

fn property(input: NomSpan) -> IResult<NomSpan, Property, BindParserError> {
    let key = ws(compound_identifier);
    let separator = ws(map_err(tag("="), BindParserError::Assignment));
    let (input, (key, value)) = separated_pair(key, separator, condition_value)(input)?;
    Ok((input, Property { key, value }))
}

fn device_specification(input: NomSpan) -> IResult<NomSpan, DeviceSpecification, BindParserError> {
    let (input, properties) = many_until_eof(ws(property))(input)?;
    Ok((input, DeviceSpecification { properties }))
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::make_identifier;
    use crate::parser_common::test::check_result;

    mod properties {
        use super::*;

        #[test]
        fn simple() {
            check_result(
                property(NomSpan::new("abc = 5")),
                "",
                Property { key: make_identifier!["abc"], value: Value::NumericLiteral(5) },
            );
        }

        #[test]
        fn simple_from_pair() {
            assert_eq!(
                property_from_pair(NomSpan::new("abc"), NomSpan::new("5")).unwrap().1,
                Property { key: make_identifier!["abc"], value: Value::NumericLiteral(5) },
            );
        }

        #[test]
        fn invalid() {
            assert_eq!(
                property(NomSpan::new("abc 5")),
                Err(nom::Err::Error(BindParserError::Assignment("5".to_string())))
            );

            assert_eq!(
                property(NomSpan::new("abc =")),
                Err(nom::Err::Error(BindParserError::ConditionValue("".to_string())))
            );

            assert_eq!(
                property(NomSpan::new("= 5")),
                Err(nom::Err::Error(BindParserError::Identifier("= 5".to_string())))
            );
        }

        #[test]
        fn invalid_from_pair() {
            assert_eq!(
                property_from_pair(NomSpan::new("abc def"), NomSpan::new("5")),
                Err(nom::Err::Error(BindParserError::Eof(" def".to_string())))
            );

            assert_eq!(
                property_from_pair(NomSpan::new("_abc"), NomSpan::new("5")),
                Err(nom::Err::Error(BindParserError::Identifier("_abc".to_string())))
            );

            assert_eq!(
                property_from_pair(NomSpan::new("abc"), NomSpan::new("5 42")),
                Err(nom::Err::Error(BindParserError::Eof(" 42".to_string())))
            );

            assert_eq!(
                property_from_pair(NomSpan::new("abc"), NomSpan::new("@")),
                Err(nom::Err::Error(BindParserError::ConditionValue("@".to_string())))
            );
        }

        #[test]
        fn empty() {
            assert_eq!(
                property(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::Identifier("".to_string())))
            );
        }

        #[test]
        fn empty_from_pair() {
            assert_eq!(
                property_from_pair(NomSpan::new(""), NomSpan::new("5")),
                Err(nom::Err::Error(BindParserError::Identifier("".to_string())))
            );

            assert_eq!(
                property_from_pair(NomSpan::new("abc"), NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::ConditionValue("".to_string())))
            );
        }
    }

    mod device_specifications {
        use super::*;

        #[test]
        fn simple() {
            check_result(
                device_specification(NomSpan::new("abc = 5\nxyz = true")),
                "",
                DeviceSpecification {
                    properties: vec![
                        Property { key: make_identifier!["abc"], value: Value::NumericLiteral(5) },
                        Property { key: make_identifier!["xyz"], value: Value::BoolLiteral(true) },
                    ],
                },
            );
        }

        #[test]
        fn empty() {
            check_result(
                device_specification(NomSpan::new("")),
                "",
                DeviceSpecification { properties: Vec::new() },
            );
        }
    }
}
