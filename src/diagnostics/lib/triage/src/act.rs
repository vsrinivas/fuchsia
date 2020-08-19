// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        config::DiagnosticData,
        metrics::{Fetcher, FileDataFetcher, Metric, MetricState, MetricValue, Metrics},
    },
    serde::{self, Deserialize},
    std::collections::HashMap,
};

/// Provides the [metric_state] context to evaluate [Action]s and results of the [actions].
pub struct ActionContext<'a> {
    actions: &'a Actions,
    metric_state: MetricState<'a>,
    action_results: ActionResults,
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
            metric_state: MetricState::new(metrics, Fetcher::FileData(fetcher)),
            action_results,
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
}

impl ActionResults {
    pub fn new() -> ActionResults {
        ActionResults { results: HashMap::new(), warnings: Vec::new(), gauges: Vec::new() }
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

    pub fn get_warnings(&self) -> &Vec<String> {
        &self.warnings
    }

    pub fn get_gauges(&self) -> &Vec<String> {
        &self.gauges
    }
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

impl Gauge {
    pub fn get_formatted_value(&self, metric_value: MetricValue) -> String {
        match metric_value {
            MetricValue::Float(value) => match &self.format {
                Some(format) => match format.as_str() {
                    "percentage" => format!("{:.2}%", value * 100.0f64),
                    _ => format!("{}", value),
                },
                _ => format!("{}", value),
            },
            MetricValue::Int(value) => format!("{}", value),
            value => format!("{:?}", value),
        }
    }
}

impl Action {
    pub fn get_tag(&self) -> Option<String> {
        match self {
            Action::Warning(action) => action.tag.clone(),
            Action::Gauge(action) => action.tag.clone(),
        }
    }
}

impl ActionContext<'_> {
    /// Processes all actions, acting on the ones that trigger.
    pub fn process(&mut self) -> &ActionResults {
        for (namespace, actions) in self.actions.iter() {
            for (name, action) in actions.iter() {
                match action {
                    Action::Warning(warning) => self.update_warnings(warning, namespace, name),
                    Action::Gauge(gauge) => self.update_gauges(gauge, namespace, name),
                };
            }
        }
        &self.action_results
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
                    .add_warning(format!("[ERROR] In config '{}': {}", namespace, reason));
                false
            }
            other => {
                self.action_results
                    .add_warning(format!("[ERROR] In config '{}': {}", namespace, other));
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
        crate::metrics::{Metric, Metrics},
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
        assert!(!includes(results.get_warnings(), "False was fired"));
    }

    #[test]
    fn gauges_fire_correctly() {
        let mut eval_file = HashMap::new();
        eval_file.insert("gauge1".to_string(), Metric::Eval("2 / 5".to_string()));
        eval_file.insert("gauge2".to_string(), Metric::Eval("4 / 5".to_string()));
        eval_file.insert("gauge3".to_string(), Metric::Eval("6 / 5".to_string()));
        let mut metrics = Metrics::new();
        metrics.insert("file".to_string(), eval_file);
        let mut actions = Actions::new();
        let mut action_file = ActionsSchema::new();
        action_file.insert(
            "gauge1".to_string(),
            Action::Gauge(Gauge {
                value: Metric::Eval("gauge1".to_string()),
                format: None,
                tag: None,
            }),
        );
        action_file.insert(
            "gauge2".to_string(),
            Action::Gauge(Gauge {
                value: Metric::Eval("gauge2".to_string()),
                format: Some("percentage".to_string()),
                tag: None,
            }),
        );
        action_file.insert(
            "gauge3".to_string(),
            Action::Gauge(Gauge {
                value: Metric::Eval("gauge3".to_string()),
                format: Some("unknown".to_string()),
                tag: None,
            }),
        );
        actions.insert("file".to_string(), action_file);
        let no_data = Vec::new();
        let mut context = ActionContext::new(&metrics, &actions, &no_data);

        let results = context.process();

        assert!(includes(results.get_gauges(), "gauge1: 0.4"));
        assert!(includes(results.get_gauges(), "gauge2: 80.00%"));
        assert!(includes(results.get_gauges(), "gauge3: 1.2"));
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
}
