// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
use log::error;

use {
    super::{
        config::DiagnosticData,
        metrics::{Fetcher, FileDataFetcher, Metric, MetricState, MetricValue, Metrics},
        plugins::{register_plugins, Plugin},
    },
    injectable_time::UtcTime,
    lazy_static::lazy_static,
    serde::{self, Deserialize},
    std::collections::HashMap,
};

/// Provides the [metric_state] context to evaluate [Action]s and results of the [actions].
pub struct ActionContext<'a> {
    actions: &'a Actions,
    metric_state: MetricState<'a>,
    action_results: ActionResults,
    plugins: Vec<Box<dyn Plugin>>,
}

lazy_static! {
    pub static ref REAL_CLOCK: UtcTime = UtcTime::new();
}

impl<'a> ActionContext<'a> {
    pub fn new(
        metrics: &'a Metrics,
        actions: &'a Actions,
        diagnostic_data: &'a Vec<DiagnosticData>,
    ) -> ActionContext<'a> {
        let fetcher = FileDataFetcher::new(diagnostic_data);
        let mut action_results = ActionResults::new();
        fetcher.errors().iter().for_each(|e| {
            action_results.add_warning(format!("[ERROR] {}", e));
        });
        ActionContext {
            actions,
            metric_state: MetricState::new(metrics, Fetcher::FileData(fetcher), &*REAL_CLOCK),
            action_results,
            plugins: register_plugins(),
        }
    }
}

/// Stores the results of each [Action] specified in [source] and
/// the [warnings] and [gauges] that are generated.
#[derive(Clone)]
pub struct ActionResults {
    results: HashMap<String, bool>,
    warnings: Vec<String>,
    gauges: Vec<String>,
    snapshots: Vec<SnapshotTrigger>,
    sort_gauges: bool,
    sub_results: Vec<(String, Box<ActionResults>)>,
}

impl ActionResults {
    pub fn new() -> ActionResults {
        ActionResults {
            results: HashMap::new(),
            warnings: Vec::new(),
            gauges: Vec::new(),
            snapshots: Vec::new(),
            sort_gauges: true,
            sub_results: Vec::new(),
        }
    }

    pub fn set_result(&mut self, action: &str, value: bool) {
        self.results.insert(action.to_string(), value);
    }

    pub fn add_warning(&mut self, warning: String) {
        self.warnings.push(warning);
    }

    pub fn add_gauge(&mut self, gauge: String) {
        self.gauges.push(gauge);
    }

    pub fn add_snapshot(&mut self, snapshot: SnapshotTrigger) {
        self.snapshots.push(snapshot);
    }

    pub fn set_sort_gauges(&mut self, v: bool) {
        self.sort_gauges = v;
    }

    pub fn sort_gauges(&self) -> bool {
        self.sort_gauges
    }

    pub fn get_warnings(&self) -> &Vec<String> {
        &self.warnings
    }

    pub fn get_gauges(&self) -> &Vec<String> {
        &self.gauges
    }

    pub fn get_sub_results_mut(&mut self) -> &mut Vec<(String, Box<ActionResults>)> {
        &mut self.sub_results
    }

    pub fn get_sub_results(&self) -> &Vec<(String, Box<ActionResults>)> {
        &self.sub_results
    }
}

/// [SnapshotTrigger] is the information needed to generate a request for a crash report.
/// It can be returned from the library as part of ActionResults.
#[derive(Debug, Clone, PartialEq)]
pub struct SnapshotTrigger {
    pub interval: i64, // zx::Duration but this library has to run on host.
    pub signature: String,
}

/// [Actions] are stored as a map of maps, both with string keys. The outer key
/// is the namespace for the inner key, which is the name of the [Action].
pub type Actions = HashMap<String, ActionsSchema>;

/// [ActionsSchema] stores the [Action]s from a single config file / namespace.
///
/// This struct is used to deserialize the [Action]s from the JSON-formatted
/// config file.
pub type ActionsSchema = HashMap<String, Action>;

/// Action represent actions that can be taken using an evaluated value(s).
#[derive(Clone, Debug, Deserialize)]
#[serde(tag = "type")]
pub enum Action {
    Warning(Warning),
    Gauge(Gauge),
    Snapshot(Snapshot),
}

/// Action that is triggered if a predicate is met.
#[derive(Clone, Debug, Deserialize)]
pub struct Warning {
    pub trigger: Metric, // An expression to evaluate which determines if this action triggers.
    pub print: String,   // What to print if trigger is true
    pub tag: Option<String>, // An optional tag to associate with this Action
}

/// Action that displays percentage of value.
#[derive(Clone, Debug, Deserialize)]
pub struct Gauge {
    pub value: Metric,          // Value to surface
    pub format: Option<String>, // Opaque type that determines how value should be formatted (e.g. percentage)
    pub tag: Option<String>,    // An optional tag to associate with this Action
}

/// Action that displays percentage of value.
#[derive(Clone, Debug, Deserialize)]
pub struct Snapshot {
    pub trigger: Metric, // Take snapshot when this is true
    pub repeat: Metric,  // Expression evaluating to time delay before repeated triggers
    pub signature: String, // Sent in the crash report
                         // There's no tag option because snapshot conditions are always news worth seeing.
}

impl Gauge {
    pub fn get_formatted_value(&self, metric_value: MetricValue) -> String {
        match metric_value {
            MetricValue::Float(value) => match &self.format {
                Some(format) if format.as_str() == "percentage" => {
                    format!("{:.2}%", value * 100.0f64)
                }
                _ => format!("{}", value),
            },
            MetricValue::Int(value) => match &self.format {
                Some(format) if format.as_str() == "percentage" => format!("{}%", value * 100),
                _ => format!("{}", value),
            },
            value => format!("{:?}", value),
        }
    }
}

impl Action {
    pub fn get_tag(&self) -> Option<String> {
        match self {
            Action::Warning(action) => action.tag.clone(),
            Action::Gauge(action) => action.tag.clone(),
            Action::Snapshot(_) => None,
        }
    }
}

impl ActionContext<'_> {
    /// Processes all actions, acting on the ones that trigger.
    pub fn process(&mut self) -> &ActionResults {
        if let Fetcher::FileData(file_data) = &self.metric_state.fetcher {
            for plugin in &self.plugins {
                self.action_results
                    .sub_results
                    .push((plugin.display_name().to_string(), Box::new(plugin.run(file_data))));
            }
        }

        for (namespace, actions) in self.actions.iter() {
            for (name, action) in actions.iter() {
                match action {
                    Action::Warning(warning) => self.update_warnings(warning, namespace, name),
                    Action::Gauge(gauge) => self.update_gauges(gauge, namespace, name),
                    Action::Snapshot(snapshot) => self.update_snapshots(snapshot, namespace, name),
                };
            }
        }

        &self.action_results
    }

    /// Evaluate and return snapshots. Consume self.
    pub fn into_snapshots(mut self) -> Vec<SnapshotTrigger> {
        for (namespace, actions) in self.actions.iter() {
            for (name, action) in actions.iter() {
                if let Action::Snapshot(snapshot) = action {
                    self.update_snapshots(snapshot, namespace, name)
                }
            }
        }
        self.action_results.snapshots
    }

    /// Update warnings if condition is met.
    fn update_warnings(&mut self, action: &Warning, namespace: &String, name: &String) {
        let was_triggered = match self.metric_state.eval_action_metric(namespace, &action.trigger) {
            MetricValue::Bool(true) => {
                self.action_results.add_warning(format!("[WARNING] {}.", action.print));
                true
            }
            MetricValue::Bool(false) => false,
            MetricValue::Missing(reason) => {
                self.action_results
                    .add_warning(format!("[MISSING] In config '{}': {}", namespace, reason));
                false
            }
            other => {
                self.action_results.add_warning(format!(
                    "[ERROR] Unexpected value type in config '{}' (need boolean): {}",
                    namespace, other
                ));
                false
            }
        };
        self.action_results.set_result(&format!("{}::{}", namespace, name), was_triggered);
    }

    /// Update snapshots if condition is met.
    fn update_snapshots(&mut self, action: &Snapshot, namespace: &str, name: &str) {
        let was_triggered = match self.metric_state.eval_action_metric(namespace, &action.trigger) {
            MetricValue::Bool(true) => {
                let interval = self.metric_state.eval_action_metric(namespace, &action.repeat);
                match interval {
                    MetricValue::Int(interval) => {
                        let signature = action.signature.clone();
                        let output = SnapshotTrigger { interval, signature };
                        self.action_results.add_snapshot(output);
                        true
                    }
                    _ => {
                        self.action_results.add_warning(format!(
                            "Bad interval in config '{}': {:?}",
                            namespace, interval
                        ));
                        #[cfg(target_os = "fuchsia")]
                        error!("Bad interval in config '{}': {:?}", namespace, interval);
                        false
                    }
                }
            }
            MetricValue::Bool(false) => false,
            MetricValue::Missing(reason) => {
                #[cfg(target_os = "fuchsia")]
                error!("Snapshot trigger was missing: {}", reason);
                self.action_results
                    .add_warning(format!("[MISSING] In config '{}': {}", namespace, reason));
                false
            }
            other => {
                #[cfg(target_os = "fuchsia")]
                error!(
                    "[ERROR] Unexpected value type in config '{}' (need boolean): {}",
                    namespace, other
                );
                self.action_results.add_warning(format!(
                    "[ERROR] Unexpected value type in config '{}' (need boolean): {}",
                    namespace, other
                ));
                false
            }
        };
        self.action_results.set_result(&format!("{}::{}", namespace, name), was_triggered);
    }

    /// Update gauges.
    fn update_gauges(&mut self, action: &Gauge, namespace: &String, name: &String) {
        let value = self.metric_state.eval_action_metric(namespace, &action.value);
        self.action_results.add_gauge(format!("{}: {}", name, action.get_formatted_value(value)));
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::config::Source,
        crate::metrics::{fetch::SelectorString, Metric, Metrics},
        anyhow::Error,
        std::convert::TryFrom,
    };

    /// Tells whether any of the stored values include a substring.
    fn includes(values: &Vec<String>, substring: &str) -> bool {
        for value in values {
            if value.contains(substring) {
                return true;
            }
        }
        return false;
    }

    #[test]
    fn actions_fire_correctly() {
        let mut eval_file = HashMap::new();
        eval_file.insert("true".to_string(), Metric::Eval("0==0".to_string()));
        eval_file.insert("false".to_string(), Metric::Eval("0==1".to_string()));
        eval_file.insert("true_array".to_string(), Metric::Eval("[0==0]".to_string()));
        eval_file.insert("false_array".to_string(), Metric::Eval("[0==1]".to_string()));
        let mut metrics = Metrics::new();
        metrics.insert("file".to_string(), eval_file);
        let mut actions = Actions::new();
        let mut action_file = ActionsSchema::new();
        action_file.insert(
            "do_true".to_string(),
            Action::Warning(Warning {
                trigger: Metric::Eval("true".to_string()),
                print: "True was fired".to_string(),
                tag: None,
            }),
        );
        action_file.insert(
            "do_false".to_string(),
            Action::Warning(Warning {
                trigger: Metric::Eval("false".to_string()),
                print: "False was fired".to_string(),
                tag: None,
            }),
        );
        action_file.insert(
            "do_true_array".to_string(),
            Action::Warning(Warning {
                trigger: Metric::Eval("true_array".to_string()),
                print: "True array was fired".to_string(),
                tag: None,
            }),
        );
        action_file.insert(
            "do_false_array".to_string(),
            Action::Warning(Warning {
                trigger: Metric::Eval("false_array".to_string()),
                print: "False array was fired".to_string(),
                tag: None,
            }),
        );

        action_file.insert(
            "do_operation".to_string(),
            Action::Warning(Warning {
                trigger: Metric::Eval("0 < 10".to_string()),
                print: "Inequality triggered".to_string(),
                tag: None,
            }),
        );
        actions.insert("file".to_string(), action_file);
        let no_data = Vec::new();
        let mut context = ActionContext::new(&metrics, &actions, &no_data);
        let results = context.process();
        assert!(includes(results.get_warnings(), "[WARNING] True was fired"));
        assert!(includes(results.get_warnings(), "[WARNING] Inequality triggered"));
        assert!(includes(results.get_warnings(), "[WARNING] True array was fired"));
        assert!(!includes(results.get_warnings(), "False was fired"));
        assert!(!includes(results.get_warnings(), "False array was fired"));
    }

    #[test]
    fn gauges_fire_correctly() {
        let mut eval_file = HashMap::new();
        eval_file.insert("gauge_f1".to_string(), Metric::Eval("2 / 5".to_string()));
        eval_file.insert("gauge_f2".to_string(), Metric::Eval("4 / 5".to_string()));
        eval_file.insert("gauge_f3".to_string(), Metric::Eval("6 / 5".to_string()));
        eval_file.insert("gauge_i4".to_string(), Metric::Eval("9 // 2".to_string()));
        eval_file.insert("gauge_i5".to_string(), Metric::Eval("11 // 2".to_string()));
        eval_file.insert("gauge_i6".to_string(), Metric::Eval("13 // 2".to_string()));
        eval_file.insert("gauge_b7".to_string(), Metric::Eval("2 == 2".to_string()));
        eval_file.insert("gauge_b8".to_string(), Metric::Eval("2 > 2".to_string()));
        eval_file.insert("gauge_s9".to_string(), Metric::Eval("'foo'".to_string()));
        let mut metrics = Metrics::new();
        metrics.insert("file".to_string(), eval_file);
        let mut actions = Actions::new();
        let mut action_file = ActionsSchema::new();
        macro_rules! insert_gauge {
            ($name:expr, $format:expr) => {
                action_file.insert(
                    $name.to_string(),
                    Action::Gauge(Gauge {
                        value: Metric::Eval($name.to_string()),
                        format: $format,
                        tag: None,
                    }),
                );
            };
        }
        insert_gauge!("gauge_f1", None);
        insert_gauge!("gauge_f2", Some("percentage".to_string()));
        insert_gauge!("gauge_f3", Some("unknown".to_string()));
        insert_gauge!("gauge_i4", None);
        insert_gauge!("gauge_i5", Some("percentage".to_string()));
        insert_gauge!("gauge_i6", Some("unknown".to_string()));
        insert_gauge!("gauge_b7", None);
        insert_gauge!("gauge_b8", None);
        insert_gauge!("gauge_s9", None);
        actions.insert("file".to_string(), action_file);
        let no_data = Vec::new();
        let mut context = ActionContext::new(&metrics, &actions, &no_data);

        let results = context.process();

        assert!(includes(results.get_gauges(), "gauge_f1: 0.4"));
        assert!(includes(results.get_gauges(), "gauge_f2: 80.00%"));
        assert!(includes(results.get_gauges(), "gauge_f3: 1.2"));
        assert!(includes(results.get_gauges(), "gauge_i4: 4"));
        assert!(includes(results.get_gauges(), "gauge_i5: 500%"));
        assert!(includes(results.get_gauges(), "gauge_i6: 6"));
        assert!(includes(results.get_gauges(), "gauge_b7: Bool(true)"));
        assert!(includes(results.get_gauges(), "gauge_b8: Bool(false)"));
        assert!(includes(results.get_gauges(), "gauge_s9: String(\"foo\")"));
    }

    #[test]
    fn action_context_errors() {
        let metrics = Metrics::new();
        let actions = Actions::new();
        let data = vec![DiagnosticData::new(
            "inspect.json".to_string(),
            Source::Inspect,
            r#"
            [
                {
                    "moniker": "abcd",
                    "payload": {"root": {"val": 10}}
                },
                {
                    "moniker": "abcd2",
                    "payload": null
                }
            ]
            "#
            .to_string(),
        )
        .expect("create data")];
        let action_context = ActionContext::new(&metrics, &actions, &data);

        assert_eq!(
            &vec!["[ERROR] Unable to deserialize Inspect contents for abcd2 to node hierarchy"
                .to_string()],
            action_context.action_results.get_warnings()
        );
    }

    #[test]
    fn snapshots_update_correctly() -> Result<(), Error> {
        let metrics = Metrics::new();
        let actions = Actions::new();
        let data = vec![];
        let mut action_context = ActionContext::new(&metrics, &actions, &data);
        let selector =
            Metric::Selector(SelectorString::try_from("INSPECT:foo:bar:baz".to_string())?);
        let true_value = Metric::Eval("1==1".to_string());
        let false_value = Metric::Eval("1==2".to_string());
        let five_value = Metric::Eval("5".to_string());
        let foo_value = Metric::Eval("'foo'".to_string());
        let missing_value = Metric::Eval("foo".to_string());
        let snapshot_5_sig = SnapshotTrigger { interval: 5, signature: "signature".to_string() };
        // Tester re-uses the same action_context, so results will accumulate.
        macro_rules! tester {
            ($trigger:expr, $repeat:expr, $func:expr) => {
                let selector_interval_action = Snapshot {
                    trigger: $trigger.clone(),
                    repeat: $repeat.clone(),
                    signature: "signature".to_string(),
                };
                action_context.update_snapshots(&selector_interval_action, "", "");
                assert!($func(&action_context.action_results.snapshots));
            };
        }
        type VT = Vec<SnapshotTrigger>;

        // Verify it doesn't crash on bad inputs
        tester!(true_value, selector, |s: &VT| s.is_empty());
        tester!(true_value, foo_value, |s: &VT| s.is_empty());
        tester!(true_value, missing_value, |s: &VT| s.is_empty());
        tester!(selector, five_value, |s: &VT| s.is_empty());
        tester!(foo_value, five_value, |s: &VT| s.is_empty());
        tester!(five_value, five_value, |s: &VT| s.is_empty());
        tester!(missing_value, five_value, |s: &VT| s.is_empty());
        assert_eq!(action_context.action_results.warnings.len(), 7);
        // False trigger shouldn't add a result
        tester!(false_value, five_value, |s: &VT| s.is_empty());
        tester!(true_value, five_value, |s| s == &vec![snapshot_5_sig.clone()]);
        // We can have more than one of the same trigger in the results.
        tester!(true_value, five_value, |s| s
            == &vec![snapshot_5_sig.clone(), snapshot_5_sig.clone()]);
        assert_eq!(action_context.action_results.warnings.len(), 7);
        Ok(())
    }
}
