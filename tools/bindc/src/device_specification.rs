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
pub struct Ast {
    pub properties: Vec<Property>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Property {
    pub key: CompoundIdentifier,
    pub value: Value,
}

impl FromStr for Ast {
    type Err = BindParserError;

    fn from_str(input: &str) -> Result<Self, Self::Err> {
        match device_specification(NomSpan::new(input)) {
            Ok((_, ast)) => Ok(ast),
            Err(nom::Err::Error(e)) => Err(e),
            Err(nom::Err::Failure(e)) => Err(e),
            Err(nom::Err::Incomplete(_)) => {
                unreachable!("Parser should never generate Incomplete errors")
            }
        }
    }
}

fn property(input: NomSpan) -> IResult<NomSpan, Property, BindParserError> {
    let key = ws(compound_identifier);
    let separator = ws(map_err(tag("="), BindParserError::Assignment));
    let (input, (key, value)) = separated_pair(key, separator, condition_value)(input)?;
    Ok((input, Property { key, value }))
}

fn device_specification(input: NomSpan) -> IResult<NomSpan, Ast, BindParserError> {
    let (input, properties) = many_until_eof(ws(property))(input)?;
    Ok((input, Ast { properties }))
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
        fn empty() {
            assert_eq!(
                property(NomSpan::new("")),
                Err(nom::Err::Error(BindParserError::Identifier("".to_string())))
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
                Ast {
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
                Ast { properties: Vec::new() },
            );
        }
    }
}
