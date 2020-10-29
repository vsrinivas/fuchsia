// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{helpers::analyze_logs, Plugin},
    crate::{act::ActionResults, metrics::FileDataFetcher},
    regex::Regex,
};

pub struct RoutingErrorsPlugin {}

impl Plugin for RoutingErrorsPlugin {
    fn name(&self) -> &'static str {
        "routing_errors"
    }

    fn display_name(&self) -> &'static str {
        "Routing Errors"
    }

    fn run(&self, inputs: &FileDataFetcher<'_>) -> ActionResults {
        let mut results = ActionResults::new();

        let re = Regex::new(r".*\[([^:]+):[0-9]+\] ERROR.*Failed to route protocol `([^`]+)` with target component `([^`]+)`.*").expect("regex compilation failed");
        analyze_logs(inputs, re, |mut pattern_match| {
            let (moniker, protocol, name): (&str, &str, &str) =
                match (pattern_match.pop(), pattern_match.pop(), pattern_match.pop()) {
                    (Some(moniker), Some(protocol), Some(name)) => {
                        (moniker.into(), protocol.into(), name.into())
                    }
                    _ => {
                        results.add_warning(
                            "[ERROR] Routing Errors plugin encountered a bug analyzing \
                    log line, capture group missing"
                                .to_string(),
                        );
                        return;
                    }
                };
            if pattern_match.len() == 0 {
                results.add_warning(
                    "[ERROR] Routing Errors plugin encountered a bug analyzing log \
                line, capture group missing"
                        .to_string(),
                );
                return;
            }
            let log_line: &str = pattern_match.remove(0).into();
            results.add_warning(format!(
                "[WARNING]: Error routing capability \"{}\" to component identified as \"{}\" \
                 with moniker \"{}\". Original error log:\n{}",
                protocol, name, moniker, log_line
            ));
        });
        results
    }
}

#[cfg(test)]
mod tests {
    use {
        super::RoutingErrorsPlugin,
        crate::{
            metrics::{fetch::TextFetcher, FileDataFetcher},
            plugins::Plugin,
        },
    };

    #[test]
    fn test_routing_errors() {
        let expected_output = vec![
            "[WARNING]: Error routing capability \
        \"fidl.examples.routing.echo.Echo\" to component identified as \"echo_client\" \
        with moniker \"/bootstrap:0/echo:0/echo_client:0\". Original error log:\n[00017.480623]\
        [1150][1253][echo_client:0] ERROR: Failed to route protocol \
        `fidl.examples.routing.echo.Echo` with target component `/bootstrap:0/echo:0/echo_client:0`\
        : A `use from parent` declaration was found at `/bootstrap:0/echo:0/echo_client:0` for \
        `fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in the \
        parent"
                .to_string(),
            "[WARNING]: Error routing capability \
        \"foo.bar.Baz\" to component identified as \"foobar\" \
        with moniker \"/bootstrap:0/foobar:345\". Original error log:\n[12471.623480]\
        [782][9443][foobar:345] ERROR: Failed to route protocol \
        `foo.bar.Baz` with target component `/bootstrap:0/foobar:345`: A `use from parent` \
        declaration was found at `/bootstrap:0/foobar:345` for `foo.bar.Baz`, but no matching \
        `offer` declaration was found in the parent"
                .to_string(),
        ];

        let fetcher: TextFetcher = r#"
[00017.480623][1150][1253][echo_client:0] ERROR: Failed to route protocol `fidl.examples.routing.echo.Echo` with target component `/bootstrap:0/echo:0/echo_client:0`: A `use from parent` declaration was found at `/bootstrap:0/echo:0/echo_client:0` for `fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in the parent
[12471.623480][782][9443][foobar:345] ERROR: Failed to route protocol `foo.bar.Baz` with target component `/bootstrap:0/foobar:345`: A `use from parent` declaration was found at `/bootstrap:0/foobar:345` for `foo.bar.Baz`, but no matching `offer` declaration was found in the parent
"#.into();

        let diagnostics_data = Vec::new();
        let mut plugin_input = FileDataFetcher::new(&diagnostics_data);
        plugin_input.syslog = &fetcher;
        assert_eq!(RoutingErrorsPlugin {}.run(&plugin_input).get_warnings(), &expected_output);
    }

    #[test]
    fn test_no_match_routing_error() {
        let expected_output: Vec<String> = vec![];

        let fetcher: TextFetcher = r#"
[00017.480623][1150][1253][component manager] ERROR: Failed to route protocol `fidl.examples.routing.echo.Echo` with target component `/bootstrap:0/echo:0/echo_client:0`: A `use from parent` declaration was found at `/bootstrap:0/echo:0/echo_client:0` for `fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in the parent
"#.into();

        let diagnostics_data = Vec::new();
        let mut plugin_input = FileDataFetcher::new(&diagnostics_data);
        plugin_input.syslog = &fetcher;
        assert_eq!(RoutingErrorsPlugin {}.run(&plugin_input).get_warnings(), &expected_output);
    }
}
