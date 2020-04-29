// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::act::ActionResults;

pub struct ActionResultFormatter<'a> {
    action_results: Vec<&'a ActionResults>,
}

impl<'a> ActionResultFormatter<'a> {
    pub fn new(action_results: Vec<&ActionResults>) -> ActionResultFormatter<'_> {
        ActionResultFormatter { action_results }
    }

    pub fn to_warnings(&self) -> String {
        let mut output = String::new();
        for action_results in &self.action_results {
            output.push_str(&format!("Warnings for {}\n", action_results.source));
            for warning in action_results.get_warnings() {
                output.push_str(&format!("{}\n", warning));
            }
        }
        output
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn action_result_formatter_to_warnings() {
        let warnings = String::from(
            "Warnings for inspect1\n\
            w1\n\
            w2\n\
            Warnings for inspect2\n\
            w3\n\
            w4\n",
        );

        let mut action_results_1 = ActionResults::new("inspect1");
        action_results_1.add_warning(String::from("w1"));
        action_results_1.add_warning(String::from("w2"));

        let mut action_results_2 = ActionResults::new("inspect2");
        action_results_2.add_warning(String::from("w3"));
        action_results_2.add_warning(String::from("w4"));

        let formatter = ActionResultFormatter::new(vec![&action_results_1, &action_results_2]);

        assert_eq!(warnings, formatter.to_warnings());
    }
}
