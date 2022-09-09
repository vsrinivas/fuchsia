// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
use tracing::{error, warn};

use {
    super::{
        config::DiagnosticData,
        metrics::{
            fetch::{Fetcher, FileDataFetcher},
            metric_value::{MetricValue, Problem},
            ExpressionContext, ExpressionTree, Function, Metric, MetricState, Metrics, ValueSource,
        },
        plugins::{register_plugins, Plugin},
    },
    crate::metric_value_to_int,
    anyhow::{bail, Error},
    fidl_fuchsia_feedback::MAX_CRASH_SIGNATURE_LENGTH,
    serde::{self, Deserialize, Serialize},
    std::{cell::RefCell, collections::HashMap},
};

/// Provides the [metric_state] context to evaluate [Action]s and results of the [actions].
pub struct ActionContext<'a> {
    actions: &'a Actions,
    metric_state: MetricState<'a>,
    action_results: ActionResults,
    plugins: Vec<Box<dyn Plugin>>,
}

impl<'a> ActionContext<'a> {
    pub(crate) fn new(
        metrics: &'a Metrics,
        actions: &'a Actions,
        diagnostic_data: &'a Vec<DiagnosticData>,
        now: Option<i64>,
    ) -> ActionContext<'a> {
        let fetcher = FileDataFetcher::new(diagnostic_data);
        let mut action_results = ActionResults::new();
        fetcher.errors().iter().for_each(|e| {
            action_results.add_error(format!("[DEBUG: BAD DATA] {}", e));
        });
        ActionContext {
            actions,
            metric_state: MetricState::new(metrics, Fetcher::FileData(fetcher), now),
            action_results,
            plugins: register_plugins(),
        }
    }
}

/// Stores the results of each [Action] specified in [source] and
/// the [warnings] and [gauges] that are generated.
#[derive(Clone, Debug)]
pub struct ActionResults {
    infos: Vec<String>,
    warnings: Vec<String>,
    errors: Vec<String>,
    gauges: Vec<String>,
    snapshots: Vec<SnapshotTrigger>,
    sort_gauges: bool,
    sub_results: Vec<(String, Box<ActionResults>)>,
}

impl ActionResults {
    pub fn new() -> ActionResults {
        ActionResults {
            infos: Vec::new(),
            warnings: Vec::new(),
            errors: Vec::new(),
            gauges: Vec::new(),
            snapshots: Vec::new(),
            sort_gauges: true,
            sub_results: Vec::new(),
        }
    }

    pub fn add_info(&mut self, info: String) {
        self.infos.push(info);
    }

    pub fn add_warning(&mut self, warning: String) {
        self.warnings.push(warning);
    }

    pub fn add_error(&mut self, error: String) {
        self.errors.push(error);
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

    pub fn get_infos(&self) -> &Vec<String> {
        &self.infos
    }

    pub fn get_warnings(&self) -> &Vec<String> {
        &self.warnings
    }

    pub fn get_errors(&self) -> &Vec<String> {
        &self.errors
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
pub(crate) type Actions = HashMap<String, ActionsSchema>;

/// [ActionsSchema] stores the [Action]s from a single config file / namespace.
///
/// This struct is used to deserialize the [Action]s from the JSON-formatted
/// config file.
pub(crate) type ActionsSchema = HashMap<String, Action>;

/// Action represent actions that can be taken using an evaluated value(s).
#[derive(Clone, Debug, Serialize, PartialEq)]
#[serde(tag = "type")]
pub enum Action {
    Alert(Alert),
    Gauge(Gauge),
    Snapshot(Snapshot),
}

impl<'de> Deserialize<'de> for Action {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        /// TODO(fxbug.dev/73441): `Warning` will be deprecated once all config files use `Alert`
        #[derive(Debug, Deserialize)]
        struct DeWarning {
            /// A wrapped expression to evaluate which determines if this action triggers.
            pub trigger: ValueSource,
            /// What to print if trigger is true.
            pub print: String,
            /// Describes where bugs should be filed if this action triggers.
            pub file_bug: Option<String>,
            /// An optional tag to associate with this Action.
            pub tag: Option<String>,
        }

        #[derive(Debug, Deserialize)]
        #[serde(tag = "type")]
        enum DeAction {
            Alert(Alert),
            Warning(DeWarning),
            Gauge(Gauge),
            Snapshot(Snapshot),
        }

        Ok(match DeAction::deserialize(deserializer)? {
            DeAction::Warning(warning) => {
                let DeWarning { trigger, print, file_bug, tag } = warning;
                Action::Alert(Alert { trigger, print, file_bug, tag, severity: Severity::Warning })
            }
            DeAction::Alert(alert) => Action::Alert(alert),
            DeAction::Gauge(gauge) => Action::Gauge(gauge),
            DeAction::Snapshot(snapshot) => Action::Snapshot(snapshot),
        })
    }
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
//#[serde(tag = "severity")]
pub enum Severity {
    Info,
    Warning,
    Error,
}

pub(crate) fn validate_actions(actions: &ActionsSchema) -> Result<(), Error> {
    for (action_name, action) in actions {
        match action {
            // Make sure the snapshot signature isn't too long.
            Action::Snapshot(snapshot) => {
                if snapshot.signature.len() > MAX_CRASH_SIGNATURE_LENGTH as usize {
                    bail!("Signature too long in {}", action_name);
                }
                // Make sure repeat is a const int expression (cache the value if so)
                match &snapshot.repeat.metric {
                    Metric::Eval(repeat_expression) => {
                        let repeat_value = MetricState::evaluate_const_expression(
                            &repeat_expression.parsed_expression,
                        );
                        if let MetricValue::Int(repeat_int) = repeat_value {
                            snapshot
                                .repeat
                                .cached_value
                                .borrow_mut()
                                .replace(MetricValue::Int(repeat_int));
                        } else {
                            bail!(
                                "Snapshot {} repeat expression '{}' must evaluate to int, not {:?}",
                                action_name,
                                repeat_expression.raw_expression,
                                repeat_value
                            );
                        }
                    }
                    _ => unreachable!("ValueSource::try_from() only produces an Eval"),
                }
            }
            // Make sure Error-level alerts have a file_bug field.
            Action::Alert(alert) => {
                if alert.severity == Severity::Error && alert.file_bug == None {
                    bail!("Error severity requires file_bug field in {}", action_name);
                }
            }
            _ => {}
        }
    }
    Ok(())
}

/// Action that is triggered if a predicate is met.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Alert {
    /// A wrapped expression to evaluate which determines if this action triggers.
    pub trigger: ValueSource,
    /// What to print if trigger is true.
    pub print: String,
    /// Describes where bugs should be filed if this action triggers.
    pub file_bug: Option<String>,
    /// An optional tag to associate with this Action.
    pub tag: Option<String>,
    /// Info, Warning, Error, with the same meanings as the log types. Error must have a file_bug:
    /// field but that field is optional for Info and Warning.
    pub severity: Severity,
}

impl<'de> Deserialize<'de> for ValueSource {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        Ok(ValueSource::try_from_expression(&String::deserialize(deserializer)?)
            .map_err(serde::de::Error::custom)?)
    }
}

/// Action that displays percentage of value.
#[derive(Clone, Debug, Serialize, Deserialize, PartialEq)]
pub struct Gauge {
    /// Value to surface.
    pub value: ValueSource,
    /// Opaque type that determines how value should be formatted (e.g. percentage).
    pub format: Option<String>,
    /// An optional tag to associate with this Action.
    pub tag: Option<String>,
}

/// Action that displays percentage of value.
#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
pub struct Snapshot {
    /// Take snapshot when this is true.
    pub trigger: ValueSource,
    /// A wrapped expression evaluating to time delay before repeated triggers.
    pub repeat: ValueSource,
    /// Sent in the crash report.
    pub signature: String,
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
            MetricValue::Problem(Problem::Ignore(_)) => "N/A".to_string(),
            value => format!("{:?}", value),
        }
    }
}

impl Action {
    pub fn get_tag(&self) -> Option<String> {
        match self {
            Action::Alert(action) => action.tag.clone(),
            Action::Gauge(action) => action.tag.clone(),
            Action::Snapshot(_) => None,
        }
    }

    /// Creates a [Warning] with a trigger evaluating to Bool(true) and its cache pre-populated.
    pub fn new_synthetic_warning(print: String) -> Action {
        let trigger_true = get_trigger_true();
        Action::Alert(Alert {
            trigger: trigger_true,
            print,
            file_bug: None,
            tag: None,
            severity: Severity::Warning,
        })
    }

    pub fn new_synthetic_error(print: String, file_bug: String) -> Action {
        let trigger_true = get_trigger_true();
        Action::Alert(Alert {
            trigger: trigger_true,
            print,
            file_bug: Some(file_bug),
            tag: None,
            severity: Severity::Error,
        })
    }

    /// Creates a [Gauge] with the cache value pre-populated.
    /// This only supports string values.
    pub fn new_synthetic_string_gauge(
        raw_value: String,
        format: Option<String>,
        tag: Option<String>,
    ) -> Action {
        let value = ValueSource {
            metric: Metric::Eval(ExpressionContext {
                raw_expression: format!("'{}'", raw_value),
                parsed_expression: ExpressionTree::Value(MetricValue::String(raw_value.clone())),
            }),
            cached_value: RefCell::new(Some(MetricValue::String(raw_value))),
        };
        Action::Gauge(Gauge { value, format, tag })
    }

    /// Returns true if any significant problem or notification is found.
    /// If the trigger or value hasnn't been evaluated, returns false
    pub(crate) fn has_reportable_issue(&self) -> bool {
        let value = match self {
            Action::Alert(alert) => &alert.trigger.cached_value,
            Action::Snapshot(snapshot) => &snapshot.trigger.cached_value,
            Action::Gauge(gauge) => &gauge.value.cached_value,
        };
        let reportable_on_true = match self {
            Action::Gauge(_) => false,
            Action::Snapshot(_) => true,
            Action::Alert(alert) if alert.severity == Severity::Info => false,
            Action::Alert(_) => true,
        };
        let result = match *value.borrow() {
            Some(MetricValue::Bool(true)) if reportable_on_true => true,
            Some(MetricValue::Problem(Problem::Missing(_))) => false,
            Some(MetricValue::Problem(Problem::Ignore(_))) => false,
            Some(MetricValue::Problem(_)) => true,
            _ => false,
        };
        result
    }
}

fn get_trigger_true() -> ValueSource {
    ValueSource {
        metric: Metric::Eval(ExpressionContext {
            raw_expression: "True()".to_string(),
            parsed_expression: ExpressionTree::Function(Function::True, vec![]),
        }),
        cached_value: RefCell::new(Some(MetricValue::Bool(true))),
    }
}

/// Contains all Error, Warning, and Info generated while computing snapshots.
pub type WarningVec = Vec<String>;

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
                    Action::Alert(alert) => self.update_alerts(alert, namespace, name),
                    Action::Gauge(gauge) => self.update_gauges(gauge, namespace, name),
                    Action::Snapshot(snapshot) => self.update_snapshots(snapshot, namespace, name),
                };
            }
        }

        &self.action_results
    }

    /// Evaluate and return snapshots. Consume self.
    pub fn into_snapshots(mut self) -> (Vec<SnapshotTrigger>, WarningVec) {
        for (namespace, actions) in self.actions.iter() {
            for (name, action) in actions.iter() {
                if let Action::Snapshot(snapshot) = action {
                    self.update_snapshots(snapshot, namespace, name)
                }
            }
        }
        let mut alerts = vec![];
        alerts.extend(self.action_results.errors);
        alerts.extend(self.action_results.warnings);
        alerts.extend(self.action_results.infos);
        (self.action_results.snapshots, alerts)
    }

    /// Update warnings if condition is met.
    fn update_alerts(&mut self, action: &Alert, namespace: &String, name: &String) {
        match self.metric_state.eval_action_metric(namespace, &action.trigger) {
            MetricValue::Bool(true) => {
                if let Some(file_bug) = &action.file_bug {
                    self.action_results.add_error(format!("[BUG:{}] {}.", file_bug, action.print));
                } else {
                    self.action_results.add_warning(format!("[WARNING] {}.", action.print));
                }
            }
            MetricValue::Bool(false) => (),
            MetricValue::Problem(Problem::Ignore(_)) => (),
            MetricValue::Problem(Problem::Missing(reason)) => {
                self.action_results.add_info(format!(
                    "[MISSING] In config '{}::{}': (need boolean trigger) {:?}",
                    namespace, name, reason,
                ));
            }
            MetricValue::Problem(problem) => {
                self.action_results.add_error(format!(
                    "[ERROR] In config '{}::{}': (need boolean trigger): {:?}",
                    namespace, name, problem,
                ));
            }
            other => {
                self.action_results.add_error(format!(
                    "[DEBUG: BAD CONFIG] Unexpected value type in config '{}::{}' (need boolean trigger): {}",
                    namespace,
                    name,
                    other,
                ));
            }
        };
    }

    /// Update snapshots if condition is met.
    fn update_snapshots(&mut self, action: &Snapshot, namespace: &str, name: &str) {
        match self.metric_state.eval_action_metric(namespace, &action.trigger) {
            MetricValue::Bool(true) => {
                let repeat_value = self.metric_state.eval_action_metric(namespace, &action.repeat);
                let interval = metric_value_to_int(repeat_value);
                match interval {
                    Ok(interval) => {
                        let signature = action.signature.clone();
                        let output = SnapshotTrigger { interval, signature };
                        self.action_results.add_snapshot(output);
                    }
                    Err(ref bad_type) => {
                        self.action_results.add_error(format!(
                            "Bad interval in config '{}::{}': {:?}",
                            namespace, name, bad_type,
                        ));
                        #[cfg(target_os = "fuchsia")]
                        error!("Bad interval in config '{}::{}': {:?}", namespace, name, interval);
                    }
                }
            }
            MetricValue::Bool(false) => (),
            MetricValue::Problem(Problem::Ignore(_)) => (),
            MetricValue::Problem(reason) => {
                #[cfg(target_os = "fuchsia")]
                warn!(
                    "Snapshot trigger was not boolean in config '{}::{}': {:?}",
                    namespace, name, reason,
                );
                self.action_results.add_info(format!(
                    "[MISSING] In config '{}::{}': {:?}",
                    namespace, name, reason,
                ));
            }
            other => {
                #[cfg(target_os = "fuchsia")]
                error!(
                    "[DEBUG: BAD CONFIG] Unexpected value type in config '{}::{}' (need boolean): {}",
                    namespace,
                    name,
                    other,
                );
                self.action_results.add_error(format!(
                    "[DEBUG: BAD CONFIG] Unexpected value type in config '{}::{}' (need boolean): {}",
                    namespace,
                    name,
                    other,
                ));
            }
        };
    }

    /// Update gauges.
    fn update_gauges(&mut self, action: &Gauge, namespace: &String, name: &String) {
        let value = self.metric_state.eval_action_metric(namespace, &action.value);
        match value {
            MetricValue::Problem(Problem::Ignore(_)) => {
                self.action_results.add_gauge(format!("{}: N/A", name));
            }
            value => {
                self.action_results.add_gauge(format!(
                    "{}: {}",
                    name,
                    action.get_formatted_value(value)
                ));
            }
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::config::Source,
        crate::metrics::{ExpressionContext, Metric, Metrics, ValueSource},
        anyhow::Error,
        std::{cell::RefCell, convert::TryFrom},
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

    #[fuchsia::test]
    fn actions_fire_correctly() {
        let mut eval_file = HashMap::new();
        eval_file.insert("true".to_string(), ValueSource::try_from_expression("0==0").unwrap());
        eval_file.insert("false".to_string(), ValueSource::try_from_expression("0==1").unwrap());
        eval_file
            .insert("true_array".to_string(), ValueSource::try_from_expression("[0==0]").unwrap());
        eval_file
            .insert("false_array".to_string(), ValueSource::try_from_expression("[0==1]").unwrap());
        let mut metrics = Metrics::new();
        metrics.insert("file".to_string(), eval_file);
        let mut actions = Actions::new();
        let mut action_file = ActionsSchema::new();
        action_file.insert(
            "do_true".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("true").unwrap(),
                print: "True was fired".to_string(),
                file_bug: Some("Some>Monorail>Component".to_string()),
                tag: None,
                severity: Severity::Warning,
            }),
        );
        action_file.insert(
            "do_false".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("false").unwrap(),
                print: "False was fired".to_string(),
                file_bug: None,
                tag: None,
                severity: Severity::Warning,
            }),
        );
        action_file.insert(
            "do_true_array".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("true_array").unwrap(),
                print: "True array was fired".to_string(),
                file_bug: None,
                tag: None,
                severity: Severity::Warning,
            }),
        );
        action_file.insert(
            "do_false_array".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("false_array").unwrap(),
                print: "False array was fired".to_string(),
                file_bug: None,
                tag: None,
                severity: Severity::Warning,
            }),
        );

        action_file.insert(
            "do_operation".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("0 < 10").unwrap(),
                print: "Inequality triggered".to_string(),
                file_bug: None,
                tag: None,
                severity: Severity::Warning,
            }),
        );
        actions.insert("file".to_string(), action_file);
        let no_data = Vec::new();
        let mut context = ActionContext::new(&metrics, &actions, &no_data, None);
        let results = context.process();
        assert!(includes(results.get_errors(), "[BUG:Some>Monorail>Component] True was fired."));
        assert!(includes(results.get_warnings(), "[WARNING] Inequality triggered."));
        assert!(includes(results.get_warnings(), "[WARNING] True array was fired"));
        assert!(!includes(results.get_warnings(), "False was fired"));
        assert!(!includes(results.get_warnings(), "False array was fired"));
    }

    #[fuchsia::test]
    fn gauges_fire_correctly() {
        let mut eval_file = HashMap::new();
        eval_file
            .insert("gauge_f1".to_string(), ValueSource::try_from_expression("2 / 5").unwrap());
        eval_file
            .insert("gauge_f2".to_string(), ValueSource::try_from_expression("4 / 5").unwrap());
        eval_file
            .insert("gauge_f3".to_string(), ValueSource::try_from_expression("6 / 5").unwrap());
        eval_file
            .insert("gauge_i4".to_string(), ValueSource::try_from_expression("9 // 2").unwrap());
        eval_file
            .insert("gauge_i5".to_string(), ValueSource::try_from_expression("11 // 2").unwrap());
        eval_file
            .insert("gauge_i6".to_string(), ValueSource::try_from_expression("13 // 2").unwrap());
        eval_file
            .insert("gauge_b7".to_string(), ValueSource::try_from_expression("2 == 2").unwrap());
        eval_file
            .insert("gauge_b8".to_string(), ValueSource::try_from_expression("2 > 2").unwrap());
        eval_file
            .insert("gauge_s9".to_string(), ValueSource::try_from_expression("'foo'").unwrap());
        let mut metrics = Metrics::new();
        metrics.insert("file".to_string(), eval_file);
        let mut actions = Actions::new();
        let mut action_file = ActionsSchema::new();
        macro_rules! insert_gauge {
            ($name:expr, $format:expr) => {
                action_file.insert(
                    $name.to_string(),
                    Action::Gauge(Gauge {
                        value: ValueSource::try_from_expression($name).unwrap(),
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
        let mut context = ActionContext::new(&metrics, &actions, &no_data, None);

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

    #[fuchsia::test]
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
                    "payload": ["a", "b"]
                },
                {
                    "moniker": "abcd3",
                    "payload": null
                }
            ]
            "#
            .to_string(),
        )
        .expect("create data")];
        let action_context = ActionContext::new(&metrics, &actions, &data, None);
        // Caution - test footgun! This error will show up without calling process() but
        // most get_warnings() results will not.
        assert_eq!(
            &vec!["[DEBUG: BAD DATA] Unable to deserialize Inspect contents for abcd2 to node hierarchy"
                .to_string()],
            action_context.action_results.get_errors()
        );
    }

    #[fuchsia::test]
    fn time_propagates_correctly() {
        let metrics = Metrics::new();
        let mut actions = Actions::new();
        let mut action_file = ActionsSchema::new();
        action_file.insert(
            "time_1234".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("Now() == 1234").unwrap(),
                print: "1234".to_string(),
                tag: None,
                file_bug: None,
                severity: Severity::Warning,
            }),
        );
        action_file.insert(
            "time_missing".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("Problem(Now())").unwrap(),
                print: "missing".to_string(),
                tag: None,
                file_bug: None,
                severity: Severity::Warning,
            }),
        );
        actions.insert("file".to_string(), action_file);
        let data = vec![];
        let actions_missing = actions.clone();
        let mut context_1234 = ActionContext::new(&metrics, &actions, &data, Some(1234));
        let results_1234 = context_1234.process();
        let mut context_missing = ActionContext::new(&metrics, &actions_missing, &data, None);
        let results_no_time = context_missing.process();

        assert_eq!(&vec!["[WARNING] 1234.".to_string()], results_1234.get_warnings());
        assert!(results_no_time
            .get_infos()
            .contains(&"[MISSING] In config \'file::time_1234\': (need boolean trigger) \"No valid time available\"".to_string()));
        assert!(results_no_time.get_warnings().contains(&"[WARNING] missing.".to_string()));
    }

    #[fuchsia::test]
    fn snapshots_update_correctly() -> Result<(), Error> {
        let metrics = Metrics::new();
        let actions = Actions::new();
        let data = vec![];
        let mut action_context = ActionContext::new(&metrics, &actions, &data, None);
        let true_value = ValueSource::try_from_expression("1==1")?;
        let false_value = ValueSource::try_from_expression("1==2")?;
        let five_value = ValueSource {
            metric: Metric::Eval(ExpressionContext::try_from("5".to_string())?),
            cached_value: RefCell::new(Some(MetricValue::Int(5))),
        };
        let foo_value = ValueSource::try_from_expression("'foo'")?;
        let missing_value = ValueSource::try_from_expression("foo")?;
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
        tester!(true_value, foo_value, |s: &VT| s.is_empty());
        tester!(true_value, missing_value, |s: &VT| s.is_empty());
        tester!(foo_value, five_value, |s: &VT| s.is_empty());
        tester!(five_value, five_value, |s: &VT| s.is_empty());
        tester!(missing_value, five_value, |s: &VT| s.is_empty());
        // Problem::Missing shows up in infos, not warnings
        assert_eq!(action_context.action_results.infos.len(), 1);
        assert_eq!(action_context.action_results.warnings.len(), 0);
        assert_eq!(action_context.action_results.errors.len(), 4);
        // False trigger shouldn't add a result
        tester!(false_value, five_value, |s: &VT| s.is_empty());
        tester!(true_value, five_value, |s| s == &vec![snapshot_5_sig.clone()]);
        // We can have more than one of the same trigger in the results.
        tester!(true_value, five_value, |s| s
            == &vec![snapshot_5_sig.clone(), snapshot_5_sig.clone()]);
        assert_eq!(action_context.action_results.infos.len(), 1);
        assert_eq!(action_context.action_results.warnings.len(), 0);
        assert_eq!(action_context.action_results.errors.len(), 4);
        let (snapshots, warnings) = action_context.into_snapshots();
        assert_eq!(snapshots.len(), 2);
        assert_eq!(warnings.len(), 5);
        Ok(())
    }

    #[fuchsia::test]
    fn actions_cache_correctly() {
        let mut eval_file = HashMap::new();
        eval_file.insert("true".to_string(), ValueSource::try_from_expression("0==0").unwrap());
        eval_file.insert("false".to_string(), ValueSource::try_from_expression("0==1").unwrap());
        eval_file.insert("five".to_string(), ValueSource::try_from_expression("5").unwrap());
        let mut metrics = Metrics::new();
        metrics.insert("file".to_string(), eval_file);
        let mut actions = Actions::new();
        let mut action_file = ActionsSchema::new();
        action_file.insert(
            "true_warning".to_string(),
            Action::Alert(Alert {
                trigger: ValueSource::try_from_expression("true").unwrap(),
                print: "True was fired".to_string(),
                file_bug: None,
                tag: None,
                severity: Severity::Warning,
            }),
        );
        action_file.insert(
            "false_gauge".to_string(),
            Action::Gauge(Gauge {
                value: ValueSource::try_from_expression("false").unwrap(),
                format: None,
                tag: None,
            }),
        );
        action_file.insert(
            "true_snapshot".to_string(),
            Action::Snapshot(Snapshot {
                trigger: ValueSource::try_from_expression("true").unwrap(),
                repeat: ValueSource {
                    metric: Metric::Eval(ExpressionContext::try_from("five".to_string()).unwrap()),
                    cached_value: RefCell::new(Some(MetricValue::Int(5))),
                },
                signature: "signature".to_string(),
            }),
        );
        action_file.insert(
            "test_snapshot".to_string(),
            Action::Snapshot(Snapshot {
                trigger: ValueSource::try_from_expression("true").unwrap(),
                repeat: ValueSource::try_from_expression("five").unwrap(),
                signature: "signature".to_string(),
            }),
        );
        actions.insert("file".to_string(), action_file);
        let no_data = Vec::new();
        let mut context = ActionContext::new(&metrics, &actions, &no_data, None);
        context.process();

        // Ensure Alert caches correctly
        if let Action::Alert(warning) = actions.get("file").unwrap().get("true_warning").unwrap() {
            assert_eq!(*warning.trigger.cached_value.borrow(), Some(MetricValue::Bool(true)));
        } else {
            unreachable!("'true_warning' must be an Action::Alert")
        }

        // Ensure Gauge caches correctly
        if let Action::Gauge(gauge) = actions.get("file").unwrap().get("false_gauge").unwrap() {
            assert_eq!(*gauge.value.cached_value.borrow(), Some(MetricValue::Bool(false)));
        } else {
            unreachable!("'false_gauge' must be an Action::Gauge")
        }

        // Ensure Snapshot caches correctly
        if let Action::Snapshot(snapshot) =
            actions.get("file").unwrap().get("true_snapshot").unwrap()
        {
            assert_eq!(*snapshot.trigger.cached_value.borrow(), Some(MetricValue::Bool(true)));
            assert_eq!(*snapshot.repeat.cached_value.borrow(), Some(MetricValue::Int(5)));
        } else {
            unreachable!("'true_snapshot' must be an Action::Snapshot")
        }

        // Ensure value-calculation does not fail for a Snapshot with an empty cache.
        // The cached value for 'repeat' is expected to be pre-calculated during deserialization
        // however, an empty cached value should still be supported.
        if let Action::Snapshot(snapshot) =
            actions.get("file").unwrap().get("test_snapshot").unwrap()
        {
            assert_eq!(*snapshot.trigger.cached_value.borrow(), Some(MetricValue::Bool(true)));
            assert_eq!(*snapshot.repeat.cached_value.borrow(), Some(MetricValue::Int(5)));
        } else {
            unreachable!("'true_snapshot' must be an Action::Snapshot")
        }
    }
}
