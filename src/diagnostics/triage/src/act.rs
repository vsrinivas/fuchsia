// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        config::InspectContext,
        metrics::{MetricState, MetricValue, Metrics},
    },
    anyhow::Error,
    log::*,
    serde::Deserialize,
    std::collections::HashMap,
};

/// Reports an [Error] to stdout and logs at "error" level.
pub fn report_failure(e: Error) {
    error!("Triage failed: {}", e);
    println!("Triage failed: {}", e);
}

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
        inspect_context: &'a InspectContext,
    ) -> ActionContext<'a> {
        ActionContext {
            actions,
            metric_state: MetricState::new(metrics, &inspect_context.data),
            action_results: ActionResults::new(&inspect_context.source),
        }
    }
}

/// Stores the results of each [Action] specified in [source] and the [warnings] that are
/// generated.
pub struct ActionResults {
    pub source: String,
    results: HashMap<String, bool>,
    warnings: Vec<String>,
}

impl ActionResults {
    pub fn new(source: &str) -> ActionResults {
        ActionResults { source: source.to_string(), results: HashMap::new(), warnings: Vec::new() }
    }

    pub fn set_result(&mut self, action: &str, value: bool) {
        self.results.insert(action.to_string(), value);
    }

    pub fn get_result(&self, action: &str) -> Option<&bool> {
        self.results.get(action)
    }

    pub fn get_actions(&self) -> Vec<String> {
        self.results.keys().map(|k| k.clone()).collect()
    }

    pub fn add_warning(&mut self, warning: String) {
        self.warnings.push(warning);
    }

    pub fn get_warnings(&self) -> &Vec<String> {
        &self.warnings
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

/// [Action] stores the specification for an action. [trigger] should name a
/// [Metric] that calculates a Boolean value. If the [Metric] is true, then
/// the string from [print] will be printed as a warning.
#[derive(Deserialize, Debug)]
pub struct Action {
    pub trigger: String, // The name of a boolean Metric
    pub print: String,   // What to print if trigger is true
}

impl ActionContext<'_> {
    /// Processes all actions, acting on the ones that trigger.
    pub fn process(&mut self) -> &ActionResults {
        for (namespace, actions) in self.actions.iter() {
            for (name, action) in actions.iter() {
                self.consider(action, namespace, name);
            }
        }
        &self.action_results
    }

    fn consider(&mut self, action: &Action, namespace: &String, name: &String) {
        let was_triggered = match self.metric_state.metric_value(namespace, &action.trigger) {
            MetricValue::Bool(true) => {
                self.act(namespace, name, &action);
                true
            }
            MetricValue::Bool(false) => false,
            other => {
                self.warn(format!(
                    "ERROR: In '{}', action '{}' used bad metric '{}' with value '{:?}'",
                    namespace, name, action.trigger, other
                ));
                false
            }
        };
        self.action_results.set_result(&format!("{}::{}", namespace, name), was_triggered);
    }

    fn warn(&mut self, warning: String) {
        self.action_results.add_warning(warning);
    }

    fn act(&mut self, namespace: &String, name: &String, action: &Action) {
        self.warn(format!(
            "Warning: '{}' in '{}' detected '{}': '{}' was true",
            name, namespace, action.print, action.trigger
        ));
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::config::InspectData,
        crate::metrics::{Metric, Metrics},
    };

    /// Tells whether any of the stored warnings include a substring.
    fn warnings_include(warnings: &Vec<String>, substring: &str) -> bool {
        for warning in warnings {
            if warning.contains(substring) {
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
            Action { trigger: "true".to_string(), print: "True was fired".to_string() },
        );
        action_file.insert(
            "do_false".to_string(),
            Action { trigger: "false".to_string(), print: "False was fired".to_string() },
        );
        actions.insert("file".to_string(), action_file);
        let inspect_context =
            InspectContext { source: String::from("source"), data: InspectData::new(vec![]) };
        let mut context = ActionContext::new(&metrics, &actions, &inspect_context);
        let results = context.process();
        assert!(warnings_include(
            results.get_warnings(),
            "Warning: 'do_true' in 'file' detected 'True was fired': 'true' was true"
        ));
        assert!(!warnings_include(results.get_warnings(), "False was fired"));
    }
}
