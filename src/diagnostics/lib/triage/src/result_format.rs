// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::act::ActionResults;

pub struct ActionResultFormatter<'a> {
    action_results: &'a ActionResults,
}

impl<'a> ActionResultFormatter<'a> {
    pub fn new(action_results: &ActionResults) -> ActionResultFormatter<'_> {
        ActionResultFormatter { action_results }
    }

    pub fn to_text(&self) -> String {
        match self.inner_to_text() {
            (true, v) => v,
            (false, v) if v.is_empty() => "No actions were triggered. All targets OK.".to_string(),
            (false, v) => {
                vec![v, "No actions were triggered. All targets OK.".to_string()].join("\n")
            }
        }
    }

    fn inner_to_text(&self) -> (bool, String) {
        let mut sections = vec![];
        let mut warning = false;
        if let Some(gauges) = self.to_gauges() {
            sections.push(gauges);
        }
        if let Some(warnings) = self.to_warnings() {
            sections.push(warnings);
            warning = true;
        }
        if let Some((plugin_warning, plugins)) = self.to_plugins() {
            sections.push(plugins);
            warning = warning || plugin_warning
        }

        (warning, sections.join("\n"))
    }

    fn to_warnings(&self) -> Option<String> {
        if self.action_results.get_warnings().is_empty() {
            return None;
        }

        let header = Self::make_underline("Warnings");
        Some(format!("{}{}\n", header, self.action_results.get_warnings().join("\n")))
    }

    fn to_gauges(&self) -> Option<String> {
        if self.action_results.get_gauges().is_empty() {
            return None;
        }

        let mut output = String::new();

        let header = Self::make_underline("Gauges");
        output.push_str(&format!("{}", header));

        for gauge in self.action_results.get_gauges().iter() {
            output.push_str(&format!("{}\n", gauge));
        }

        Some(output)
    }

    fn to_plugins(&self) -> Option<(bool, String)> {
        let mut warning = false;
        let results = self
            .action_results
            .get_sub_results()
            .iter()
            .map(|(name, v)| {
                let fmt = ActionResultFormatter::new(&v);
                let val = match fmt.inner_to_text() {
                    (true, v) => {
                        warning = true;
                        format!("\n{}", v)
                    }
                    (false, _) => " - OK".to_string(),
                };
                format!("{} Plugin{}", name, val)
            })
            .collect::<Vec<String>>();
        if results.is_empty() {
            None
        } else {
            Some((warning, results.join("\n\n")))
        }
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
    fn action_result_formatter_to_warnings_when_no_actions_triggered() {
        let action_results = ActionResults::new();
        let formatter = ActionResultFormatter::new(&action_results);

        assert_eq!(String::from("No actions were triggered. All targets OK."), formatter.to_text());
    }

    #[test]
    fn action_result_formatter_to_text_when_actions_triggered() {
        let warnings = String::from(
            "Warnings\n\
        --------\n\
        w1\n\
        w2\n",
        );

        let mut action_results = ActionResults::new();
        action_results.add_warning(String::from("w1"));
        action_results.add_warning(String::from("w2"));

        let formatter = ActionResultFormatter::new(&action_results);

        assert_eq!(warnings, formatter.to_text());
    }

    #[test]
    fn action_result_formatter_to_text_with_gauges() {
        let warnings = String::from(
            "Gauges\n\
            ------\n\
            g1\n\n\
            Warnings\n\
        --------\n\
        w1\n\
        w2\n\
        ",
        );

        let mut action_results = ActionResults::new();
        action_results.add_warning(String::from("w1"));
        action_results.add_warning(String::from("w2"));
        action_results.add_gauge(String::from("g1"));

        let formatter = ActionResultFormatter::new(&action_results);

        assert_eq!(warnings, formatter.to_text());
    }
}
