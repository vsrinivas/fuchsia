// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        config::InspectData,
        metrics::{MetricState, MetricValue, Metrics},
    },
    failure::Error,
    log::*,
    serde_derive::Deserialize,
    std::collections::HashMap,
};

/// Reports an [Error] to stdout and logs at "error" level.
pub fn report_failure(e: Error) {
    error!("Triage failed: {}", e);
    println!("Triage failed: {}", e);
}

/// Outputs a [message] to stdout and logs at "warning" level.
pub fn output_warning(message: &String) {
    println!("{}", message);
    warn!("{}", message);
}

/// Provides the [metric_state] context to evaluate [Action]s and store the
/// [warnings] from the [actions] that trigger.
pub struct ActionContext<'a> {
    actions: &'a Actions,
    metric_state: MetricState<'a>,
    pub warnings: Warnings,
}

impl<'a> ActionContext<'a> {
    pub fn new(
        metrics: &'a Metrics,
        actions: &'a Actions,
        inspect_data: &'a InspectData,
    ) -> ActionContext<'a> {
        ActionContext {
            actions,
            metric_state: MetricState::new(metrics, inspect_data),
            warnings: Vec::new(),
        }
    }
}

type Warnings = Vec<String>;

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
    pub fn process(&mut self) {
        for (namespace, actions) in self.actions.iter() {
            for (name, action) in actions.iter() {
                self.consider(action, namespace, name);
            }
        }
    }

    fn consider(&mut self, action: &Action, namespace: &String, name: &String) {
        match self.metric_state.metric_value(namespace, &action.trigger) {
            MetricValue::Bool(true) => self.act(namespace, name, &action),
            MetricValue::Bool(false) => {}
            other => self.warn(format!(
                "ERROR: In '{}', action '{}' used bad metric '{}' with value '{:?}'",
                namespace, name, action.trigger, other
            )),
        }
    }

    fn warn(&mut self, warning: String) {
        self.warnings.push(warning);
    }

    fn act(&mut self, namespace: &String, name: &String, action: &Action) {
        self.warn(format!(
            "Warning: '{}' in '{}' detected '{}': '{}' was true",
            name, namespace, action.print, action.trigger
        ));
    }

    /// Outputs stored warnings.
    pub fn print_warnings(&self) {
        for warning in self.warnings.iter() {
            output_warning(warning);
        }
    }

    /// Tells whether any of the stored warnings includes a substring.
    #[cfg(test)]
    pub fn warnings_include(&self, substring: &str) -> bool {
        for warning in self.warnings.iter() {
            if warning.contains(substring) {
                return true;
            }
        }
        return false;
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::metrics::{Metric, MetricsSchema},
    };

    #[test]
    fn actions_fire_correctly() {
        let mut metrics = Metrics::new();
        let mut metric_file = MetricsSchema::new();
        metric_file.insert("true".to_string(), Metric::Eval("0==0".to_string()));
        metric_file.insert("false".to_string(), Metric::Eval("0==1".to_string()));
        metrics.insert("file".to_string(), metric_file);
        let inspect_entries = Vec::new();
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
        let mut context = ActionContext::new(&metrics, &actions, &inspect_entries);
        context.process();
        assert!(context.warnings_include(
            "Warning: 'do_true' in 'file' detected 'True was fired': 'true' was true"
        ));
        assert!(!context.warnings_include("False was fired"));
    }
}
