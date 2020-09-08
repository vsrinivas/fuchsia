// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::Plugin,
    crate::{
        act::ActionResults,
        metrics::{fetch::SelectorString, FileDataFetcher, MetricValue},
    },
    itertools::Itertools,
    std::convert::TryFrom,
};

pub struct MemoryPlugin();

const SELECTOR: &'static str = "INSPECT:memory_monitor.cmx:root:current_digest";

impl Plugin for MemoryPlugin {
    fn name(&self) -> &'static str {
        "memory"
    }

    fn display_name(&self) -> &'static str {
        "Memory Summary"
    }

    fn run(&self, inputs: &FileDataFetcher<'_>) -> ActionResults {
        let mut results = ActionResults::new();
        results.set_sort_gauges(false);
        let val = match inputs
            .inspect
            .fetch(&SelectorString::try_from(SELECTOR.to_string()).expect("invalid selector"))
            .into_iter()
            .next()
        {
            Some(MetricValue::String(val)) => val,
            _ => {
                // Short circuit if value could not be found. This is not an error.
                return results;
            }
        };

        val.lines()
            .filter_map(|line| {
                let mut split = line.split(": ");
                let (name, value) = (split.next(), split.next());
                match (name, value) {
                    (Some(name), Some(value)) => {
                        if value.is_empty() || name == "Free" || name == "timestamp" {
                            return None;
                        }
                        let numeric = value.trim_matches(|c: char| !c.is_digit(10));
                        let (mult, parsed) = if value.ends_with("k") {
                            (1_000f64, numeric.parse::<f64>().ok())
                        } else if value.ends_with("M") {
                            (1_000_000f64, numeric.parse::<f64>().ok())
                        } else if value.ends_with("G") {
                            (1_000_000_000f64, numeric.parse::<f64>().ok())
                        } else {
                            (1f64, numeric.parse::<f64>().ok())
                        };

                        match parsed{
                            Some(parsed) => Some((name, value, mult*parsed)),
                            None => {
                                results.add_warning(format!("[ERROR] Could not parse '{}' as a valid size. Something is wrong with the output of memory_monitor.cmx.", value));
                                None
                            }
                        }

                    }
                    _ => None,
                }
            })
            .sorted_by(|a, b| a.2.partial_cmp(&b.2).unwrap())
            .rev()
            .for_each(|entry| {
                results.add_gauge(format!("{}: {}", entry.0, entry.1));
            });

        results
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metrics::fetch::InspectFetcher;
    use std::convert::TryInto;

    #[test]
    fn test_crashes() {
        let expected_gauges: Vec<String> =
            vec!["TestCmx: 2G", "Other: 7M", "Abcd: 10.3k", "Bbb: 9999"]
                .into_iter()
                .map(|s| s.to_string())
                .collect();
        let expected_warnings: Vec<String> =
            vec!["[ERROR] Could not parse 'ABCD' as a valid size. Something is wrong with the output of memory_monitor.cmx."]
                .into_iter()
                .map(|s| s.to_string())
                .collect();
        let fetcher: InspectFetcher = r#"
[
  {
    "moniker": "memory_monitor.cmx",
    "payload": {
        "root": {
            "current_digest": "Abcd: 10.3k\nOther: 7M\nBbb: 9999\n\nTestCmx: 2G\ninvalid_line\ninvalid: \ninvalid_again: ABCD\n\nFree: 100M\ntimestamp: 10234\n\n"
        }
    }
  }
]
"#
        .try_into().expect("failed to parse inspect");

        let empty_diagnostics_vec = Vec::new();

        let mut inputs = FileDataFetcher::new(&empty_diagnostics_vec);
        inputs.inspect = &fetcher;
        let result = MemoryPlugin {}.run(&inputs);
        assert_eq!(result.get_gauges(), &expected_gauges);
        assert_eq!(result.get_warnings(), &expected_warnings);
    }
}
