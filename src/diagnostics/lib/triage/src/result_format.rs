// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::act::ActionResults;

pub struct ActionResultFormatter<'a> {
    action_results: Vec<&'a ActionResults>,
}

impl<'a> ActionResultFormatter<'a> {
    pub fn new(action_results: Vec<&ActionResults>) -> ActionResultFormatter<'_> {
        ActionResultFormatter { action_results }
    }

    pub fn to_text(&self) -> String {
        match self.to_gauges() {
            Some(gauges) => format!("{}\n{}", gauges, self.to_warnings()),
            None => self.to_warnings(),
        }
    }

    fn to_warnings(&self) -> String {
        if self.action_results.iter().all(|results| results.get_warnings().is_empty()) {
            return String::from("No actions were triggered. All targets OK.");
        }

        let mut output = String::new();

        let warning_output = self
            .action_results
            .iter()
            .filter(|results| !results.get_warnings().is_empty())
            .map(|results| {
                let header =
                    Self::make_underline(&format!("Warnings for target {}", results.source));
                format!("{}{}", header, results.get_warnings().join("\n"))
            })
            .collect::<Vec<String>>()
            .join("\n\n");
        output.push_str(&format!("{}\n", &warning_output));

        let non_warning_output = self
            .action_results
            .iter()
            .filter(|results| results.get_warnings().is_empty())
            .map(|results| {
                let header = Self::make_underline(&format!(
                    "No actions were triggered for target {}",
                    results.source
                ));
                format!("{}", header)
            })
            .collect::<Vec<String>>()
            .join("\n");
        output.push_str(&non_warning_output);

        output
    }

    fn to_gauges(&self) -> Option<String> {
        if self.action_results.iter().all(|results| results.get_gauges().is_empty()) {
            return None;
        }

        let mut output = String::new();

        let header = Self::make_underline("Gauges");
        output.push_str(&format!("{}", header));

        for results in self.action_results.iter() {
            for gauge in results.get_gauges().iter() {
                output.push_str(&format!("{}\n", gauge));
            }
        }

        Some(output)
    }

    fn make_underline(content: &str) -> String {
        let mut output = String::new();
        output.push_str(&format!("{}\n", content));
        output.push_str(&format!("{}\n", "-".repeat(content.len())));
        output
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn action_result_formatter_to_text_when_no_actions_triggered() {
        let action_results_1 = ActionResults::new("inspect1");
        let action_results_2 = ActionResults::new("inspect2");
        let formatter = ActionResultFormatter::new(vec![&action_results_1, &action_results_2]);

        assert_eq!(String::from("No actions were triggered. All targets OK."), formatter.to_text());
    }

    #[test]
    fn action_result_formatter_to_text_when_actions_triggered() {
        let warnings = String::from(
            "Warnings for target inspect1\n\
        ----------------------------\n\
        w1\n\
        w2\n\n\
        Warnings for target inspect2\n\
        ----------------------------\n\
        w3\n\
        w4\n\
        No actions were triggered for target inspect3\n\
        ---------------------------------------------\n",
        );

        let mut action_results_1 = ActionResults::new("inspect1");
        action_results_1.add_warning(String::from("w1"));
        action_results_1.add_warning(String::from("w2"));

        let mut action_results_2 = ActionResults::new("inspect2");
        action_results_2.add_warning(String::from("w3"));
        action_results_2.add_warning(String::from("w4"));

        let action_results_3 = ActionResults::new("inspect3");

        let formatter = ActionResultFormatter::new(vec![
            &action_results_1,
            &action_results_2,
            &action_results_3,
        ]);

        assert_eq!(warnings, formatter.to_text());
    }

    #[test]
    fn action_result_formatter_to_text_with_gauges() {
        let warnings = String::from(
            "Gauges\n\
            ------\n\
            g1\n\n\
            Warnings for target inspect1\n\
        ----------------------------\n\
        w1\n\
        w2\n\
        ",
        );

        let mut action_results = ActionResults::new("inspect1");
        action_results.add_warning(String::from("w1"));
        action_results.add_warning(String::from("w2"));
        action_results.add_gauge(String::from("g1"));

        let formatter = ActionResultFormatter::new(vec![&action_results]);

        assert_eq!(warnings, formatter.to_text());
    }
}
