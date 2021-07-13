// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    check::{summarize_results, PreflightCheck, PreflightCheckResult, RunSummary},
    config::*,
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_preflight_args::PreflightCommand,
    regex::Regex,
    serde_json,
    std::fmt,
    std::io::{stdout, Write},
    termion::color,
    textwrap,
};

mod analytics;
mod check;
mod command_runner;
mod config;
mod json;

// Unicode characters.
static CHECK_MARK: &str = "\u{2713}";
static BALLOT_X: &str = "\u{2717}";

// String constants for output.
static RUNNING_CHECKS_PREAMBLE: &str = "Running pre-flight checks...";
static SOME_CHECKS_FAILED_RECOVERABLE: &str =
    "Some checks failed :(. Follow the instructions above and try running again.";
static SOME_CHECKS_FAILED_FATAL: &str = "Some checks failed :(. Sorry!";
static EVERYTING_CHECKS_OUT: &str =
    "Everything checks out! Continue at https://fuchsia.dev/fuchsia-src/get-started";
static EVERYTING_CHECKS_OUT_WITH_WARNINGS: &str =
    "There were some warnings, but you can still carry on. Continue at https://fuchsia.dev/fuchsia-src/get-started";

#[cfg(target_os = "linux")]
fn get_operating_system() -> Result<OperatingSystem> {
    Ok(OperatingSystem::Linux)
}

#[cfg(target_os = "macos")]
fn get_operating_system() -> Result<OperatingSystem> {
    get_operating_system_macos(&command_runner::SYSTEM_COMMAND_RUNNER)
}

#[allow(dead_code)]
fn get_operating_system_macos(
    command_runner: &command_runner::CommandRunner,
) -> Result<OperatingSystem> {
    let (status, stdout, _) =
        (command_runner)(&vec!["defaults", "read", "loginwindow", "SystemVersionStampAsString"])
            .expect("Could not get MacOS version string");
    assert!(status.success());

    let re = Regex::new(r"(\d+)\.(\d+)(?:\.\d+)?")?;
    let caps = re.captures(&stdout).ok_or(anyhow!("unexpected output from `defaults read`"))?;
    let major: u32 = caps.get(1).unwrap().as_str().parse()?;
    let minor: u32 = caps.get(2).unwrap().as_str().parse()?;
    Ok(OperatingSystem::MacOS(major, minor))
}

#[ffx_plugin()]
pub async fn preflight_cmd(cmd: PreflightCommand) -> Result<()> {
    let config = PreflightConfig { system: get_operating_system()? };
    let checks: Vec<Box<dyn PreflightCheck>> = vec![
        Box::new(check::build_prereqs::BuildPrereqs::new(&command_runner::SYSTEM_COMMAND_RUNNER)),
        Box::new(check::femu_graphics::FemuGraphics::new(&command_runner::SYSTEM_COMMAND_RUNNER)),
        Box::new(check::emu_networking::EmuNetworking::new(&command_runner::SYSTEM_COMMAND_RUNNER)),
        Box::new(check::emu_acceleration::EmuAcceleration::new(&command_runner::SYSTEM_COMMAND_RUNNER)),
    ];

    let results = run_preflight_checks(&checks, &config).await?;
    if cmd.json {
        println!("{}", serde_json::to_string(&json::results_to_json(&results)?)?);
    } else {
        report_result_analytics(&results).await?;
        write_preflight_results(&mut stdout(), &results)?;
    }

    Ok(())
}

async fn run_preflight_checks(
    checks: &Vec<Box<dyn PreflightCheck>>,
    config: &PreflightConfig,
) -> Result<Vec<check::PreflightCheckResult>> {
    let mut results = vec![];
    for check in checks {
        results.push(check.run(&config).await?);
    }
    Ok(results)
}

async fn report_result_analytics(results: &Vec<PreflightCheckResult>) -> Result<()> {
    let summary = summarize_results(results);
    let action = match summary {
        RunSummary::Success => analytics::ANALYTICS_ACTION_SUCCESS,
        RunSummary::Warning => analytics::ANALYTICS_ACTION_WARNING,
        RunSummary::RecoverableFailure => analytics::ANALYTICS_ACTION_FAILURE_RECOVERABLE,
        RunSummary::Failure => analytics::ANALYTICS_ACTION_FAILURE,
    };
    analytics::report_preflight_analytics(action).await;
    Ok(())
}

fn write_preflight_results<W: Write>(
    writer: &mut W,
    results: &Vec<PreflightCheckResult>,
) -> Result<()> {
    writeln!(writer, "{}", RUNNING_CHECKS_PREAMBLE)?;
    writeln!(writer)?;
    for result in results.iter() {
        writeln!(writer, "{}", result)?;
    }

    let summary = summarize_results(results);

    match summary {
        RunSummary::Success => {
            writeln!(writer, "{}", EVERYTING_CHECKS_OUT)?;
        }
        RunSummary::Warning => {
            writeln!(writer, "{}", EVERYTING_CHECKS_OUT_WITH_WARNINGS)?;
        }
        RunSummary::RecoverableFailure => {
            ffx_bail!("{}", SOME_CHECKS_FAILED_RECOVERABLE);
        }
        RunSummary::Failure => {
            ffx_bail!("{}", SOME_CHECKS_FAILED_FATAL);
        }
    };
    Ok(())
}

fn wrap_text(input: &str, indent_all: bool) -> String {
    let lines = textwrap::wrap(input, 80);
    let mut indented_lines = vec![];
    for line in lines {
        indented_lines.push(textwrap::indent(
            &line,
            if indent_all || !indented_lines.is_empty() { "      " } else { "" },
        ));
    }
    indented_lines.join("")
}

impl fmt::Display for PreflightCheckResult {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PreflightCheckResult::Success(message) => write!(
                f,
                "  {}[{}]{} {}",
                color::Fg(color::Green),
                CHECK_MARK,
                color::Fg(color::Reset),
                wrap_text(message, false)
            ),
            PreflightCheckResult::Warning(message) => write!(
                f,
                "  {}[!]{} {}",
                color::Fg(color::Yellow),
                color::Fg(color::Reset),
                wrap_text(message, false)
            ),
            PreflightCheckResult::Failure(message, resolution) => {
                write!(
                    f,
                    "  {}[{}]{} {}",
                    color::Fg(color::Red),
                    BALLOT_X,
                    color::Fg(color::Reset),
                    wrap_text(message, false),
                )?;
                match resolution {
                    Some(resolution_message) => {
                        write!(f, "\n\n{}", wrap_text(resolution_message, true))
                    }
                    None => Ok(()),
                }
            }
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::command_runner::ExitStatus, async_trait::async_trait};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_parse_macos_version() -> Result<()> {
        let mut run_command: command_runner::CommandRunner = |args| {
            assert_eq!(
                args.to_vec(),
                vec!["defaults", "read", "loginwindow", "SystemVersionStampAsString"]
            );
            Ok((ExitStatus(0), "10.15.17\n\n".to_string(), "".to_string()))
        };

        assert_eq!(OperatingSystem::MacOS(10, 15), get_operating_system_macos(&run_command)?);

        run_command = |args| {
            assert_eq!(
                args.to_vec(),
                vec!["defaults", "read", "loginwindow", "SystemVersionStampAsString"]
            );
            Ok((ExitStatus(0), "11.1\n\n".to_string(), "".to_string()))
        };

        assert_eq!(OperatingSystem::MacOS(11, 1), get_operating_system_macos(&run_command)?);
        Ok(())
    }

    struct SuccessCheck {}

    #[async_trait(?Send)]
    impl PreflightCheck for SuccessCheck {
        async fn run(&self, _config: &PreflightConfig) -> Result<PreflightCheckResult> {
            Ok(PreflightCheckResult::Success("This check passed!".to_string()))
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_checks_success() -> Result<()> {
        let config = PreflightConfig { system: OperatingSystem::Linux };
        let checks: Vec<Box<dyn PreflightCheck>> = vec![Box::new(SuccessCheck {})];
        let mut buf = Vec::new();
        let results = run_preflight_checks(&checks, &config).await?;
        let result = write_preflight_results(&mut buf, &results);
        let output = String::from_utf8(buf)?;
        // Check for the various output strings.
        assert!(output.starts_with(RUNNING_CHECKS_PREAMBLE));
        assert!(output.contains("This check passed!"));
        assert!(output.contains(EVERYTING_CHECKS_OUT));
        result
    }

    struct FailPermanentCheck {}
    struct FailRecoverableCheck {}

    #[async_trait(?Send)]
    impl PreflightCheck for FailPermanentCheck {
        async fn run(&self, _config: &PreflightConfig) -> Result<PreflightCheckResult> {
            Ok(PreflightCheckResult::Failure("Oh no...".to_string(), None))
        }
    }

    #[async_trait(?Send)]
    impl PreflightCheck for FailRecoverableCheck {
        async fn run(&self, _config: &PreflightConfig) -> Result<PreflightCheckResult> {
            Ok(PreflightCheckResult::Failure(
                "We will get through this.".to_string(),
                Some("Take a deep breath and try again.".to_string()),
            ))
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_checks_fail_nonrecoverable() -> Result<()> {
        let config = PreflightConfig { system: OperatingSystem::Linux };
        let checks: Vec<Box<dyn PreflightCheck>> = vec![
            Box::new(SuccessCheck {}),
            Box::new(FailPermanentCheck {}),
            Box::new(FailRecoverableCheck {}),
        ];
        let mut buf = Vec::new();
        let results = run_preflight_checks(&checks, &config).await?;
        let result = write_preflight_results(&mut buf, &results);
        let output = String::from_utf8(buf)?;
        // Check for the various output strings.
        assert!(output.starts_with(RUNNING_CHECKS_PREAMBLE), "{:?}", output);
        assert!(output.contains("This check passed!"), "{:?}", output);
        assert!(output.contains("Oh no..."), "{:?}", output);
        match result {
            Err(error) => {
                assert!(
                    error.to_string().contains(SOME_CHECKS_FAILED_FATAL),
                    "{}",
                    error.to_string()
                );
                assert!(
                    !error.to_string().contains(SOME_CHECKS_FAILED_RECOVERABLE),
                    "{}",
                    error.to_string()
                );
                Ok(())
            }
            Ok(_) => unreachable!(),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn run_checks_fail_recoverable() -> Result<()> {
        let config = PreflightConfig { system: OperatingSystem::Linux };
        let checks: Vec<Box<dyn PreflightCheck>> = vec![
            Box::new(SuccessCheck {}),
            Box::new(FailRecoverableCheck {}),
            Box::new(FailRecoverableCheck {}),
        ];
        let mut buf = Vec::new();
        let results = run_preflight_checks(&checks, &config).await?;
        let result = write_preflight_results(&mut buf, &results);
        let output = String::from_utf8(buf)?;
        // Check for the various output strings.
        assert!(output.starts_with(RUNNING_CHECKS_PREAMBLE), "{:?}", output);
        assert!(output.contains("This check passed!"), "{:?}", output);
        assert!(output.contains("We will get through this."), "{:?}", output);
        assert!(output.contains("Take a deep breath and try again."), "{:?}", output);
        match result {
            Err(error) => {
                assert!(
                    !error.to_string().contains(SOME_CHECKS_FAILED_FATAL),
                    "{}",
                    error.to_string()
                );
                assert!(
                    error.to_string().contains(SOME_CHECKS_FAILED_RECOVERABLE),
                    "{}",
                    error.to_string()
                );
                Ok(())
            }
            Ok(_) => unreachable!(),
        }
    }
}
