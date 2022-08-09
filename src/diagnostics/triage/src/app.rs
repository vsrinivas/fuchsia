// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        config::{self, OutputFormat, ProgramStateHolder},
        Options,
    },
    anyhow::{bail, Context as _, Error},
    triage_lib::{
        analyze, analyze_structured, ActionResultFormatter, ActionResults, DiagnosticData,
        ParseResult, TriageOutput,
    },
};

/// The entry point for the CLI app.
pub struct App {
    options: Options,
}

impl App {
    /// Creates a new App with the given options.
    pub fn new(options: Options) -> App {
        App { options }
    }

    /// Runs the App.
    ///
    /// This method consumes self and calls run_structured or run_unstructured
    /// depending on if structured output is in options. It then collects the results
    /// and writes the results to the dest. If an error occurs during the running of the app
    /// it will be returned as an Error.
    pub fn run(self, dest: &mut dyn std::io::Write) -> Result<bool, Error> {
        // TODO(fxbug.dev/50449): Use 'argh' crate.
        let ProgramStateHolder { parse_result, diagnostic_data, output_format } =
            config::initialize(self.options.clone())?;

        match output_format {
            OutputFormat::Structured => {
                let structured_run_result = self.run_structured(diagnostic_data, parse_result)?;
                structured_run_result.write_report(dest)?;
                Ok(structured_run_result.has_warnings())
            }
            OutputFormat::Text => {
                let run_result =
                    self.run_unstructured(diagnostic_data, parse_result, output_format)?;
                run_result.write_report(dest)?;
                Ok(run_result.has_warnings())
            }
        }
    }

    fn run_unstructured(
        self,
        diagnostic_data: Vec<DiagnosticData>,
        parse_result: ParseResult,
        output_format: OutputFormat,
    ) -> Result<RunResult, Error> {
        let action_results = analyze(&diagnostic_data, &parse_result)?;

        Ok(RunResult::new(output_format, action_results))
    }

    fn run_structured(
        self,
        diagnostic_data: Vec<DiagnosticData>,
        parse_result: ParseResult,
    ) -> Result<StructuredRunResult, Error> {
        let triage_output = analyze_structured(&diagnostic_data, &parse_result)?;

        Ok(StructuredRunResult { triage_output })
    }
}

/// The result of calling App::run.
pub struct RunResult {
    output_format: OutputFormat,
    action_results: ActionResults,
}

impl RunResult {
    /// Creates a new RunResult struct. This method is intended to be used by the
    /// App:run method.
    fn new(output_format: OutputFormat, action_results: ActionResults) -> RunResult {
        RunResult { output_format, action_results }
    }

    /// Returns true if at least one ActionResults has a warning.
    pub fn has_warnings(&self) -> bool {
        !self.action_results.get_warnings().is_empty()
    }

    /// Writes the contents of the run to the provided writer.
    ///
    /// This method can be used to output the results to a file or stdout.
    pub fn write_report(&self, dest: &mut dyn std::io::Write) -> Result<(), Error> {
        if self.output_format != OutputFormat::Text {
            bail!("BUG: Incorrect output format requested");
        }

        let results_formatter = ActionResultFormatter::new(&self.action_results);
        let output = results_formatter.to_text();
        dest.write_fmt(format_args!("{}\n", output)).context("failed to write to destination")?;
        Ok(())
    }
}

/// The result of calling App::run_structured.
pub struct StructuredRunResult {
    triage_output: TriageOutput,
}

impl StructuredRunResult {
    /// Returns true if at least one error in TriageOutput.
    pub fn has_warnings(&self) -> bool {
        self.triage_output.has_triggered_warning()
    }

    /// Writes the contents of the run_structured to the provided writer.
    ///
    /// This method can be used to output the results to a file or stdout.
    pub fn write_report(&self, dest: &mut dyn std::io::Write) -> Result<(), Error> {
        let output = serde_json::to_string(&self.triage_output)?;
        dest.write_fmt(format_args!("{}\n", output)).context("failed to write to destination")?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        triage_lib::{Action, ActionResults},
    };

    #[fuchsia::test]
    fn test_output_text_no_warnings() -> Result<(), Error> {
        let action_results = ActionResults::new();
        let run_result = RunResult::new(OutputFormat::Text, action_results);

        let mut dest = vec![];
        run_result.write_report(&mut dest)?;

        let output = String::from_utf8(dest)?;
        assert_eq!("No actions were triggered. All targets OK.\n", output);

        Ok(())
    }

    #[fuchsia::test]
    fn test_output_text_with_warnings() -> Result<(), Error> {
        let mut action_results = ActionResults::new();
        action_results.add_warning("fail".to_string());

        let run_result = RunResult::new(OutputFormat::Text, action_results);

        let mut dest = vec![];
        run_result.write_report(&mut dest)?;

        let output = String::from_utf8(dest)?;
        assert_eq!("Warnings\n--------\nfail\n\n", output);

        Ok(())
    }

    #[fuchsia::test]
    fn test_output_text_with_gauges() -> Result<(), Error> {
        let mut action_results = ActionResults::new();
        action_results.add_gauge("gauge".to_string());
        let run_result = RunResult::new(OutputFormat::Text, action_results);

        let mut dest = vec![];
        run_result.write_report(&mut dest)?;

        let output = String::from_utf8(dest)?;
        assert_eq!("Gauges\n------\ngauge\n\nNo actions were triggered. All targets OK.\n", output);

        Ok(())
    }

    #[fuchsia::test]
    fn test_structured_output_no_warnings() -> Result<(), Error> {
        let triage_output = TriageOutput::new(Vec::new());
        let structured_run_result = StructuredRunResult { triage_output };

        let mut dest = vec![];
        structured_run_result.write_report(&mut dest)?;

        let output = String::from_utf8(dest)?;
        assert_eq!(
            "{\"actions\":{},\"metrics\":{},\"plugin_results\":{},\"triage_errors\":[]}\n",
            output
        );

        Ok(())
    }

    #[fuchsia::test]
    fn test_structured_output_with_warnings() -> Result<(), Error> {
        let mut triage_output = TriageOutput::new(vec!["file".to_string()]);
        triage_output.add_action(
            "file".to_string(),
            "warning_name".to_string(),
            Action::new_synthetic_warning("fail".to_string()),
        );
        let structured_run_result = StructuredRunResult { triage_output };

        let mut dest = vec![];
        structured_run_result.write_report(&mut dest)?;

        let output = String::from_utf8(dest)?;
        assert_eq!(
            "{\"actions\":{\"file\":{\"warning_name\":{\"type\":\"Warning\",\"trigger\":\
        {\"metric\":{\"Eval\":{\"raw_expression\":\"True()\",\"parsed_expression\":{\"Function\":\
        [\"True\",[]]}}},\"cached_value\":\
        {\"Bool\":true}},\"print\":\"fail\",\"file_bug\":null,\"tag\":null}}},\"metrics\":\
        {\"file\":{}},\"plugin_results\":{},\"triage_errors\":[]}\n",
            output
        );

        Ok(())
    }

    #[fuchsia::test]
    fn test_structured_output_with_gauges() -> Result<(), Error> {
        let mut triage_output = TriageOutput::new(vec!["file".to_string()]);
        triage_output.add_action(
            "file".to_string(),
            "gauge_name".to_string(),
            Action::new_synthetic_string_gauge("gauge".to_string(), None, None),
        );
        let structured_run_result = StructuredRunResult { triage_output };

        let mut dest = vec![];
        structured_run_result.write_report(&mut dest)?;

        let output = String::from_utf8(dest)?;
        assert_eq!(
            "{\"actions\":{\"file\":{\"gauge_name\":{\"type\":\"Gauge\",\"value\":{\"metric\":\
            {\"Eval\":{\"raw_expression\":\"'gauge'\",\"parsed_expression\":{\"Value\":\
            {\"String\":\"gauge\"}}}},\"cached_value\":{\"String\":\"gauge\"}},\"format\":null,\
            \"tag\":null}}},\"metrics\":{\"file\":{}},\"plugin_results\":{},\"triage_errors\":[]}\n",
            output
        );

        Ok(())
    }
}
