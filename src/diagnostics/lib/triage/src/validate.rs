// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        act::{Action, Actions},
        config::ParseResult,
        metrics::{
            fetch::{KeyValueFetcher, TextFetcher},
            Fetcher, MetricState, MetricValue, TrialDataFetcher,
        },
    },
    anyhow::{bail, format_err, Error},
    injectable_time::FakeTime,
    serde::Deserialize,
    serde_json as json,
    std::{collections::HashMap, convert::TryFrom},
};

#[derive(Clone, Deserialize, Debug)]
pub struct Trial {
    yes: Option<Vec<String>>,
    no: Option<Vec<String>>,
    values: Option<HashMap<String, json::Value>>,
    klog: Option<String>,
    syslog: Option<String>,
    bootlog: Option<String>,
    annotations: Option<json::map::Map<String, json::Value>>,
}

// Outer String is namespace, inner String is trial name
pub type Trials = HashMap<String, TrialsSchema>;
pub type TrialsSchema = HashMap<String, Trial>;

pub fn validate(parse_result: &ParseResult) -> Result<(), Error> {
    let ParseResult { metrics, actions, tests } = parse_result;
    let mut failed = false;
    for (namespace, trial_map) in tests {
        for (trial_name, trial) in trial_map {
            if trial.yes == None && trial.no == None {
                bail!("Trial {} in {} needs a yes: or no: entry", trial_name, namespace);
            }
            let klog_fetcher;
            let syslog_fetcher;
            let bootlog_fetcher;
            let annotations_fetcher;
            let mut fetcher = match &trial.values {
                Some(values) => Fetcher::TrialData(TrialDataFetcher::new(values)),
                None => Fetcher::TrialData(TrialDataFetcher::new_empty()),
            };
            if let Fetcher::TrialData(fetcher) = &mut fetcher {
                if let Some(klog) = &trial.klog {
                    klog_fetcher = TextFetcher::try_from(&**klog)?;
                    fetcher.set_klog(&klog_fetcher);
                }
                if let Some(syslog) = &trial.syslog {
                    syslog_fetcher = TextFetcher::try_from(&**syslog)?;
                    fetcher.set_syslog(&syslog_fetcher);
                }
                if let Some(bootlog) = &trial.bootlog {
                    bootlog_fetcher = TextFetcher::try_from(&**bootlog)?;
                    fetcher.set_bootlog(&bootlog_fetcher);
                }
                if let Some(annotations) = &trial.annotations {
                    annotations_fetcher = KeyValueFetcher::try_from(annotations)?;
                    fetcher.set_annotations(&annotations_fetcher);
                }
            }
            let time_source = FakeTime::new();
            // TODO(cphoenix): Figure out what the fake time should be for validation.
            // Tests will want a predictable ergonomic Now() time.
            let state = MetricState::new(metrics, fetcher, &time_source);
            if let Some(action_names) = &trial.yes {
                for action_name in action_names.iter() {
                    failed =
                        check_failure(namespace, trial_name, action_name, actions, &state, true)
                            || failed;
                }
            }
            if let Some(action_names) = &trial.no {
                for action_name in action_names.iter() {
                    failed =
                        check_failure(namespace, trial_name, action_name, actions, &state, false)
                            || failed;
                }
            }
        }
    }
    if failed {
        return Err(format_err!("Config validation test failed"));
    } else {
        Ok(())
    }
}

// Returns true iff the trial did NOT get the expected result.
fn check_failure(
    namespace: &String,
    trial_name: &String,
    action_name: &String,
    actions: &Actions,
    metric_state: &MetricState<'_>,
    expected: bool,
) -> bool {
    match actions.get(namespace) {
        None => {
            println!("Namespace {} not found in trial {}", action_name, trial_name);
            return true;
        }
        Some(action_map) => match action_map.get(action_name) {
            None => {
                println!("Action {} not found in trial {}", action_name, trial_name);
                return true;
            }
            Some(action) => match action {
                Action::Warning(properties) => {
                    match metric_state.eval_action_metric(namespace, &properties.trigger) {
                        MetricValue::Bool(actual) if actual == expected => return false,
                        other => {
                            println!(
                        "Test {} failed: trigger '{}' of action {} returned {:?}, expected {}",
                        trial_name, properties.trigger, action_name, other, expected
                    );
                            return true;
                        }
                    }
                }
                _ => {
                    return false;
                }
            },
        },
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::act::{Action, Warning},
        crate::metrics::Metric,
        anyhow::Error,
    };

    // Correct operation of the klog, syslog, and bootlog fields of TrialDataFetcher are tested
    // in the integration test via log_tests.triage.

    macro_rules! build_map {($($tuple:expr),*) => ({
        let mut map = HashMap::new();
        $(
            let (key, value) = $tuple;
            map.insert(key.to_string(), value);
         )*
            map
    })}

    macro_rules! create_parse_result {
        (metrics: $metrics:expr, actions: $actions:expr, tests: $tests:expr) => {
            ParseResult {
                metrics: $metrics.clone(),
                actions: $actions.clone(),
                tests: $tests.clone(),
            }
        };
    }

    #[test]
    fn validate_works() -> Result<(), Error> {
        let metrics = build_map!((
            "foo",
            build_map!(
                ("true", Metric::Eval("1==1".to_string())),
                ("false", Metric::Eval("1==0".to_string()))
            )
        ));
        let actions = build_map!((
            "foo",
            build_map!(
                (
                    "fires",
                    Action::Warning(Warning {
                        trigger: Metric::Eval("true".to_string()),
                        print: "good".to_string(),
                        tag: None
                    })
                ),
                (
                    "no_fire",
                    Action::Warning(Warning {
                        trigger: Metric::Eval("false".to_string()),
                        print: "what?!?".to_string(),
                        tag: None
                    })
                )
            )
        ));
        let good_trial = Trial {
            yes: Some(vec!["fires".to_string()]),
            no: Some(vec!["no_fire".to_string()]),
            values: Some(HashMap::new()),
            klog: None,
            syslog: None,
            bootlog: None,
            annotations: None,
        };
        assert!(validate(&create_parse_result!(
            metrics: metrics,
            actions: actions,
            tests: build_map!(("foo", build_map!(("good", good_trial))))
        ))
        .is_ok());
        // Make sure it objects if a trial that should fire doesn't.
        // Also, make sure it signals failure if there's both a good and a bad trial.
        let bad_trial = Trial {
            yes: Some(vec!["fires".to_string(), "no_fire".to_string()]),
            no: None, // Test that None doesn't crash
            values: None,
            klog: None,
            syslog: None,
            bootlog: None,
            annotations: None,
        };
        let good_trial = Trial {
            yes: Some(vec!["fires".to_string()]),
            no: Some(vec!["no_fire".to_string()]),
            values: Some(HashMap::new()),
            klog: None,
            syslog: None,
            bootlog: None,
            annotations: None,
        };
        assert!(validate(&create_parse_result!(
            metrics: metrics,
            actions: actions,
            tests: build_map!(("foo", build_map!(("good", good_trial), ("bad", bad_trial))))
        ))
        .is_err());
        // Make sure it objects if a trial fires when it shouldn't.
        let bad_trial = Trial {
            yes: Some(vec![]), // Test that empty vec works right
            no: Some(vec!["fires".to_string(), "no_fire".to_string()]),
            values: None,
            klog: None,
            syslog: None,
            bootlog: None,
            annotations: None,
        };
        assert!(validate(&create_parse_result!(
            metrics: metrics,
            actions: actions,
            tests: build_map!(("foo", build_map!(("bad", bad_trial))))
        ))
        .is_err());
        Ok(())
    }
}
