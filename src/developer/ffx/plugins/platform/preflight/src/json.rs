// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tools to convert preflight results to JSON output.
use {
    crate::check::{summarize_results, PreflightCheckResult},
    serde::Serialize,
};

#[derive(Serialize, Debug, PartialEq)]
pub struct JsonResult {
    result: String,
    checks: Vec<JsonCheckResult>,
}

#[derive(Serialize, Debug, PartialEq)]
pub struct JsonCheckResult {
    result: String,
    explanation: String,
    mitigation: Option<String>,
}

// TODO(thatguy): Consider implementing Serialize for existing types instead of
// using intermediary structs.
pub fn results_to_json(results: &Vec<PreflightCheckResult>) -> anyhow::Result<JsonResult> {
    let check_results: Vec<_> = results
        .iter()
        .map(|res| {
            let result;
            let explanation;
            let mut mitigation = None;
            match res {
                PreflightCheckResult::Success(message) => {
                    result = "success".to_string();
                    explanation = message.clone();
                }
                PreflightCheckResult::Warning(message) => {
                    result = "warning".to_string();
                    explanation = message.clone();
                }
                PreflightCheckResult::Failure(message, resolution) => {
                    result = "failure".to_string();
                    explanation = message.clone();
                    mitigation = resolution.clone();
                }
            }
            JsonCheckResult { result, explanation, mitigation }
        })
        .collect();

    Ok(JsonResult { result: summarize_results(results).to_string(), checks: check_results })
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_success() -> Result<(), anyhow::Error> {
        assert_eq!(
            results_to_json(&vec![
                PreflightCheckResult::Success("explanation".to_string()),
                PreflightCheckResult::Success("explanation2".to_string())
            ])?,
            JsonResult {
                result: "success".to_string(),
                checks: vec![
                    JsonCheckResult {
                        result: "success".to_string(),
                        explanation: "explanation".to_string(),
                        mitigation: None
                    },
                    JsonCheckResult {
                        result: "success".to_string(),
                        explanation: "explanation2".to_string(),
                        mitigation: None
                    }
                ]
            }
        );
        Ok(())
    }

    #[test]
    fn test_warning() -> Result<(), anyhow::Error> {
        assert_eq!(
            results_to_json(&vec![
                PreflightCheckResult::Success("explanation".to_string()),
                PreflightCheckResult::Warning("careful: contents hot!".to_string())
            ])?,
            JsonResult {
                result: "warning".to_string(),
                checks: vec![
                    JsonCheckResult {
                        result: "success".to_string(),
                        explanation: "explanation".to_string(),
                        mitigation: None
                    },
                    JsonCheckResult {
                        result: "warning".to_string(),
                        explanation: "careful: contents hot!".to_string(),
                        mitigation: None
                    }
                ]
            }
        );
        Ok(())
    }

    #[test]
    fn test_failure() -> Result<(), anyhow::Error> {
        assert_eq!(
            results_to_json(&vec![
                PreflightCheckResult::Success("explanation".to_string()),
                PreflightCheckResult::Warning("careful: contents hot!".to_string()),
                PreflightCheckResult::Failure("oh no...".to_string(), None),
                PreflightCheckResult::Failure(
                    "not great, but".to_string(),
                    Some("do this".to_string())
                ),
            ])?,
            JsonResult {
                result: "failure".to_string(),
                checks: vec![
                    JsonCheckResult {
                        result: "success".to_string(),
                        explanation: "explanation".to_string(),
                        mitigation: None
                    },
                    JsonCheckResult {
                        result: "warning".to_string(),
                        explanation: "careful: contents hot!".to_string(),
                        mitigation: None
                    },
                    JsonCheckResult {
                        result: "failure".to_string(),
                        explanation: "oh no...".to_string(),
                        mitigation: None
                    },
                    JsonCheckResult {
                        result: "failure".to_string(),
                        explanation: "not great, but".to_string(),
                        mitigation: Some("do this".to_string()),
                    },
                ]
            }
        );
        Ok(())
    }

    #[test]
    fn test_failure_recoverable() -> Result<(), anyhow::Error> {
        assert_eq!(
            results_to_json(&vec![
                PreflightCheckResult::Success("explanation".to_string()),
                PreflightCheckResult::Warning("careful: contents hot!".to_string()),
                PreflightCheckResult::Failure(
                    "oh no...".to_string(),
                    Some("never give up".to_string())
                ),
                PreflightCheckResult::Failure(
                    "not great, but".to_string(),
                    Some("do this".to_string())
                ),
            ])?,
            JsonResult {
                result: "recoverable_failure".to_string(),
                checks: vec![
                    JsonCheckResult {
                        result: "success".to_string(),
                        explanation: "explanation".to_string(),
                        mitigation: None
                    },
                    JsonCheckResult {
                        result: "warning".to_string(),
                        explanation: "careful: contents hot!".to_string(),
                        mitigation: None
                    },
                    JsonCheckResult {
                        result: "failure".to_string(),
                        explanation: "oh no...".to_string(),
                        mitigation: Some("never give up".to_string()),
                    },
                    JsonCheckResult {
                        result: "failure".to_string(),
                        explanation: "not great, but".to_string(),
                        mitigation: Some("do this".to_string()),
                    },
                ]
            }
        );
        Ok(())
    }
}
