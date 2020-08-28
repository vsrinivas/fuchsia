// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::Plugin,
    crate::{act::ActionResults, metrics::FileDataFetcher},
    regex::Regex,
    std::collections::BTreeSet,
};

pub struct SandboxErrorsPlugin();

impl Plugin for SandboxErrorsPlugin {
    fn name(&self) -> &'static str {
        "sandbox_errors"
    }

    fn display_name(&self) -> &'static str {
        "Sandbox Errors"
    }

    fn run(&self, inputs: &FileDataFetcher<'_>) -> ActionResults {
        let mut results = ActionResults::new();

        let mut error_tuples = BTreeSet::new();

        let re = Regex::new(r"`([^`]+)` is not allowed to connect to `([^`]+)` because this service is not present in the component's sandbox")
            .expect("regex compilation");
        for line in inputs.klog.lines.iter().chain(inputs.syslog.lines.iter()) {
            match re.captures(line) {
                Some(captures) => {
                    error_tuples.insert((
                        captures.get(1).unwrap().as_str().to_owned(),
                        captures.get(2).unwrap().as_str().to_owned(),
                    ));
                }
                _ => {}
            };
        }

        for (name, service) in error_tuples.iter() {
            results.add_warning(format!(
                "[WARNING]: {} tried to use {}, which was not declared in its sandbox",
                name, service
            ));
        }
        results
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::metrics::fetch::TextFetcher;

    #[test]
    fn test_sandbox_errors() {
        let expected_warnings: Vec<String> = vec![
            "[WARNING]: my_component.cmx tried to use fuchsia.example.Id, which was not declared in its sandbox",
            "[WARNING]: my_component.cmx tried to use fuchsia.example.Test, which was not declared in its sandbox",
            "[WARNING]: test.cmx tried to use fuchsia.example.Test, which was not declared in its sandbox",
        ]
        .into_iter()
        .map(|s| s.to_string())
        .collect::<Vec<_>>();

        // Test these cases:
        // - Sandbox failure
        // - Duplicate failure (suppressed).
        // - Same component, different service (not duplicate)
        // - Different component, same service (not duplicate)
        // - Unrelated log line.
        let fetcher: TextFetcher = r#"
[100.100] `my_component.cmx` is not allowed to connect to `fuchsia.example.Test` because this service is not present in the component's sandbox
[110.100] `my_component.cmx` is not allowed to connect to `fuchsia.example.Test` because this service is not present in the component's sandbox
[120.100] `my_component.cmx` is not allowed to connect to `fuchsia.example.Id` because this service is not present in the component's sandbox
[130.100] `test.cmx` is not allowed to connect to `fuchsia.example.Test` because this service is not present in the component's sandbox
[140.100] `test.cmx` is not allowed to connect to `component 2`
"#
        .into();

        let empty_diagnostics_vec = Vec::new();

        let mut inputs = FileDataFetcher::new(&empty_diagnostics_vec);
        inputs.klog = &fetcher;
        assert_eq!(SandboxErrorsPlugin {}.run(&inputs).get_warnings(), &expected_warnings);

        let mut inputs = FileDataFetcher::new(&empty_diagnostics_vec);
        inputs.syslog = &fetcher;
        assert_eq!(SandboxErrorsPlugin {}.run(&inputs).get_warnings(), &expected_warnings);
    }
}
