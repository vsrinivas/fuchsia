// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::{ComponentError, EvaluationError},
    crate::spec::TestCase,
    maplit::hashmap,
    std::collections::HashMap,
    std::convert::TryFrom,
    triage,
};

const TRIAGE_PRESENCE_TEMPLATE: &'static str = r#"
{
    select: {
        var: "INSPECT:{selector}",
    },
    act: {
        value_was_missing: {
            type: "Warning",
            trigger: "Missing(var)",
            print: "Selector `{selector}` did not match any data",
        }
    }
}"#;

const TRIAGE_LAMBDA_TEMPLATE: &'static str = r#"
{
    select: {
        var: "INSPECT:{selector}",
    },
    eval: {
        lambda: "Fn({lambda})",
    },
    act: {
        value_was_missing: {
            type: "Warning",
            trigger: "Not(Apply(lambda, [var]))",
            print: "Function `{lambda}` returned false for `{selector}`",
        }
    }
}"#;

/// Wrapper for the context to evaluate incoming diagnostics data.
pub(crate) struct EvaluationContext {
    /// The result of constructing and parsing a triage configuration for a specific test case.
    config_parse_result: triage::ParseResult,

    /// The original config text, for debugging.
    config_text: String,
}

impl EvaluationContext {
    pub(crate) fn run(&self, json: &str) -> Result<(), EvaluationError> {
        let data_vec = vec![triage::DiagnosticData::new(
            "inspect.json".to_string(),
            triage::Source::Inspect,
            json.to_string(),
        )
        .map_err(|e| EvaluationError::ParseFailure {
            message: e.to_string(),
            data: json.to_string(),
        })?];

        let result = triage::analyze(&data_vec, &self.config_parse_result)
            .map_err(|e| EvaluationError::InternalFailure(e.to_string()))?;
        match result.get_warnings() {
            list if list.len() == 0 => Ok(()),
            warnings => Err(EvaluationError::Failure {
                reasons: warnings.join("\n"),
                data: json.to_string(),
                config: self.config_text.to_string(),
            }),
        }
    }
}

impl TryFrom<&TestCase> for EvaluationContext {
    type Error = ComponentError;
    fn try_from(case: &TestCase) -> Result<Self, Self::Error> {
        let selector = case.selector.replace("\\", "\\\\");
        let case_config = match &case.expression {
            None => TRIAGE_PRESENCE_TEMPLATE.replace("{selector}", &selector),
            Some(expression) => TRIAGE_LAMBDA_TEMPLATE
                .replace("{selector}", &selector)
                // Lambda is of the form [var] <function using var>,
                // we format the first ']' character as "]," so that it matches the input required
                // for Fn([var], <function using var>)
                //
                // We additionally escape all instances of '"' and '\'
                .replace(
                    "{lambda}",
                    &(expression
                        .replacen("]", "],", 1)
                        .replace("\\", "\\\\")
                        .replace("\"", "\\\"")),
                ),
        };

        let configs: HashMap<String, String> = hashmap! {
            "test_case".to_string() => case_config.clone(),
        };

        let config_parse_result =
            triage::ParseResult::new(&configs, &triage::ActionTagDirective::AllowAll).map_err(
                |e| ComponentError::TriageConfigError {
                    message: e.to_string(),
                    config: case_config.clone(),
                },
            )?;

        config_parse_result.validate().map_err(|e| ComponentError::TriageConfigError {
            message: e.to_string(),
            config: case_config.clone(),
        })?;

        Ok(EvaluationContext { config_parse_result, config_text: case_config })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn evaluation_context_run() {
        // Note: Even though several cases will fail to evaluate, they pass validation of the
        // triage format.

        let cases = vec![
            (
                "a:b:c WHERE abcd",
                "a",
                r#"{"root": {"value": 10}}"#,
                Some("Apply needs a function in its first argument"),
            ),
            ("a:b:c", "a", r#"{"root": {"value": 10}}"#, Some("Selector `a:b:c` did not match")),
            ("a:root:value", "a", r#"{"root": {"value": 10}}"#, None),
            ("a:root:value WHERE [v] v > 9", "a", r#"{"root": {"value": 10}}"#, None),
            (
                "a:root:value WHERE [v] v < 9",
                "a",
                r#"{"root": {"value": 10}}"#,
                Some("returned false for `a:root:value`"),
            ),
            ("a:root:\\:value", "a", r#"{"root": {":value": 10}}"#, None),
        ];

        for (case, moniker, payload, expected_error) in cases.into_iter() {
            let context = EvaluationContext::try_from(
                &TestCase::try_from(case.to_string()).expect("parsing case"),
            )
            .expect("create context");

            let data = format!(
                r#"
                [
                    {{
                        "data_source": "Inspect",
                        "moniker": "{moniker}",
                        "payload": {payload},
                        "version": 1
                    }}
                ]
            "#,
                moniker = moniker,
                payload = payload
            );

            match expected_error {
                None => {
                    context.run(&data).expect("successful evaluation");
                }
                Some(expected_error) => match context.run(&data) {
                    Err(EvaluationError::Failure { reasons, .. }) => {
                        assert!(
                            reasons.find(expected_error).is_some(),
                            "Could not find substring {} in {}",
                            expected_error,
                            reasons
                        );
                    }
                    v => {
                        assert!(false, "Expected an internal error, found {:?}", v);
                    }
                },
            }
        }
    }
}
