// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::compiler::compiler,
    crate::debugger::device_specification::DeviceSpecification,
    crate::debugger::offline_debugger::{self, debug_from_device_specification},
    crate::errors::UserError,
    crate::parser,
    serde::Deserialize,
    serde_json,
    std::collections::HashMap,
    std::convert::TryFrom,
    std::fmt,
    thiserror::Error,
    valico::json_schema,
};

const SCHEMA: &str = include_str!("../tests_schema.json");

#[derive(Deserialize, Debug, PartialEq)]
#[serde(rename_all = "lowercase")]
enum ExpectedResult {
    Match,
    Abort,
}

#[derive(Deserialize, Debug)]
pub struct TestSpec {
    name: String,
    expected: ExpectedResult,
    device: HashMap<String, String>,
}

struct TestSuite {
    specs: Vec<TestSpec>,
}

#[derive(Debug, Error, Clone, PartialEq)]
pub enum TestError {
    BindParserError(parser::common::BindParserError),
    DeviceSpecParserError(parser::common::BindParserError),
    DebuggerError(offline_debugger::DebuggerError),
    CompilerError(compiler::CompilerError),
    InvalidSchema,
    JsonParserError(String),
    // The JSON validator unfortunately doesn't produce useful error messages.
    InvalidJsonError,
}

pub fn run(rules: &str, libraries: &[String], tests: &str) -> Result<bool, TestError> {
    TestSuite::try_from(tests).and_then(|t| t.run(rules, libraries))
}

impl TestSuite {
    fn run(&self, rules: &str, libraries: &[String]) -> Result<bool, TestError> {
        let bind_rules = compiler::compile_bind(rules, libraries, false, false, false)
            .map_err(TestError::CompilerError)?;

        for test in &self.specs {
            let mut device_specification = DeviceSpecification::new();
            for (key, value) in &test.device {
                device_specification
                    .add_property(&key, &value)
                    .map_err(TestError::DeviceSpecParserError)?;
            }

            let result = debug_from_device_specification(&bind_rules, device_specification)
                .map_err(TestError::DebuggerError)?;
            match (&test.expected, result) {
                (ExpectedResult::Match, false) => return Ok(false),
                (ExpectedResult::Abort, true) => return Ok(false),
                _ => (),
            }
        }

        Ok(true)
    }
}

impl TryFrom<&str> for TestSuite {
    type Error = TestError;

    fn try_from(input: &str) -> Result<Self, Self::Error> {
        let schema = serde_json::from_str(&SCHEMA)
            .map_err(|e| TestError::JsonParserError(format!("{}", e)))?;
        let mut scope = json_schema::Scope::new();
        let compiled_schema =
            scope.compile_and_return(schema, false).map_err(|_| TestError::InvalidSchema)?;

        let spec = serde_json::from_str(input)
            .map_err(|e| TestError::JsonParserError(format!("{}", e)))?;

        let res = compiled_schema.validate(&spec);
        if !res.is_strictly_valid() {
            return Err(TestError::InvalidJsonError);
        }

        let specs: Vec<TestSpec> = serde_json::from_value(spec)
            .map_err(|e| TestError::JsonParserError(format!("{}", e)))?;
        Ok(TestSuite { specs })
    }
}

impl fmt::Display for TestError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn parse_one_test() {
        let TestSuite { specs } = TestSuite::try_from(
            r#"
[
    {
        "name": "A test",
        "expected": "match",
        "device": {
            "key": "value"
        }
    }
]"#,
        )
        .unwrap();
        assert_eq!(specs.len(), 1);
        assert_eq!(specs[0].name, "A test".to_string());
        assert_eq!(specs[0].expected, ExpectedResult::Match);

        let mut expected_device = HashMap::new();
        expected_device.insert("key".to_string(), "value".to_string());
        assert_eq!(specs[0].device, expected_device);
    }

    #[test]
    fn parse_two_tests() {
        let TestSuite { specs } = TestSuite::try_from(
            r#"
[
    {
        "name": "A test",
        "expected": "match",
        "device": {
            "key": "value"
        }
    },
    {
        "name": "A second test",
        "expected": "abort",
        "device": {
            "key1": "value1",
            "key2": "value2"
        }
    }
]"#,
        )
        .unwrap();
        assert_eq!(specs.len(), 2);
        assert_eq!(specs[0].name, "A test".to_string());
        assert_eq!(specs[0].expected, ExpectedResult::Match);
        assert_eq!(specs[1].name, "A second test".to_string());
        assert_eq!(specs[1].expected, ExpectedResult::Abort);

        let mut expected_device = HashMap::new();
        expected_device.insert("key".to_string(), "value".to_string());
        assert_eq!(specs[0].device, expected_device);

        let mut expected_device2 = HashMap::new();
        expected_device2.insert("key1".to_string(), "value1".to_string());
        expected_device2.insert("key2".to_string(), "value2".to_string());
        assert_eq!(specs[1].device, expected_device2);
    }

    #[test]
    fn parse_json_failure() {
        match TestSuite::try_from("this can't be parsed") {
            Err(TestError::JsonParserError(_)) => (),
            _ => panic!("Expected a JsonParserError"),
        }
    }

    #[test]
    fn parse_invalid_json() {
        match TestSuite::try_from(r#"{ "valid json": "invalid to spec" }"#) {
            Err(TestError::InvalidJsonError) => (),
            _ => panic!("Expected a InvalidJsonError"),
        };
    }
}
