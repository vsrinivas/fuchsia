// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::act::ActionResults;

pub struct CSV {
    header: Vec<String>,
    rows: Vec<Vec<String>>,
}

impl CSV {
    pub fn to_string(&self, delimiter: &str) -> String {
        let mut result = String::new();
        result.push_str(&self.header.join(delimiter));
        result.push('\n');
        for row in &self.rows {
            result.push_str(&row.join(delimiter));
            result.push('\n')
        }
        result
    }
}

pub struct ActionResultFormatter<'a> {
    action_results: Vec<&'a ActionResults>,
    action_labels: Vec<String>,
}

impl<'a> ActionResultFormatter<'a> {
    pub fn new(
        action_results: Vec<&ActionResults>,
        action_labels: Vec<String>,
    ) -> ActionResultFormatter<'_> {
        ActionResultFormatter { action_results, action_labels }
    }

    pub fn to_csv(&self) -> CSV {
        let mut rows = Vec::new();

        let mut header = vec![String::from("source")];
        header.append(&mut self.action_labels.clone());

        for action_results in &self.action_results {
            rows.push(self.action_result_as_csv_row(action_results));
        }

        CSV { header, rows }
    }

    fn action_result_as_csv_row(&self, action_result: &ActionResults) -> Vec<String> {
        let mut row = vec![action_result.source.clone()];
        for col in &self.action_labels {
            match action_result.get_result(col) {
                Some(true) => row.push(String::from("true")),
                Some(false) => row.push(String::from("false")),
                _ => row.push(String::from("n/a")),
            }
        }
        row
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
    fn csv_to_string() {
        let csv_str = String::from(
            "c0,c1,c2,c3\n\
            v0,,v2,v3\n\
            v4,v5,,v7\n\
            v8,v9,v10,v11\n\
            \n\
            v12,v13,v14,v15,v16\n",
        );

        let header =
            vec![String::from("c0"), String::from("c1"), String::from("c2"), String::from("c3")];

        let rows = vec![
            vec![String::from("v0"), String::from(""), String::from("v2"), String::from("v3")],
            vec![String::from("v4"), String::from("v5"), String::from(""), String::from("v7")],
            vec![String::from("v8"), String::from("v9"), String::from("v10"), String::from("v11")],
            vec![],
            vec![
                String::from("v12"),
                String::from("v13"),
                String::from("v14"),
                String::from("v15"),
                String::from("v16"),
            ],
        ];

        let csv = CSV { header, rows };
        assert_eq!(csv.to_string(","), csv_str);
    }

    #[test]
    fn action_result_formatter_to_csv() {
        let csv_header = vec![
            String::from("source"),
            String::from("trigger1"),
            String::from("trigger2"),
            String::from("trigger3"),
        ];
        let csv_rows = vec![
            vec![
                String::from("inspect1"),
                String::from("true"),
                String::from("false"),
                String::from("n/a"),
            ],
            vec![
                String::from("inspect2"),
                String::from("n/a"),
                String::from("false"),
                String::from("true"),
            ],
        ];
        let csv = CSV { header: csv_header, rows: csv_rows };

        let mut action_results_1 = ActionResults::new("inspect1");
        action_results_1.set_result("trigger1", true);
        action_results_1.set_result("trigger2", false);

        let mut action_results_2 = ActionResults::new("inspect2");
        action_results_2.set_result("trigger2", false);
        action_results_2.set_result("trigger3", true);

        let formatter = ActionResultFormatter::new(
            vec![&action_results_1, &action_results_2],
            vec![String::from("trigger1"), String::from("trigger2"), String::from("trigger3")],
        );

        assert_eq!(csv.to_string(","), formatter.to_csv().to_string(","));
    }

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

        let formatter = ActionResultFormatter::new(
            vec![&action_results_1, &action_results_2],
            vec![String::from("trigger1"), String::from("trigger2"), String::from("trigger3")],
        );

        assert_eq!(warnings, formatter.to_warnings());
    }
}
