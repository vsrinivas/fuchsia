// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    check::{PreflightCheck, PreflightCheckResult},
    config::*,
    ffx_core::{ffx_bail, ffx_plugin},
    ffx_preflight_args::PreflightCommand,
    regex::Regex,
    std::fmt,
    std::io::{stdout, Write},
    termion::color,
    textwrap,
};

mod check;
mod command_runner;
mod config;

// Unicode characters
static CHECK_MARK: &str = "\u{2713}";
static BALLOT_X: &str = "\u{2717}";
static RUNNING_CHECKS_PREAMBLE: &str = "Running pre-flight checks...";
static SOME_CHECKS_FAILED_RECOVERABLE: &str =
    "Some checks failed :(. Follow the instructions above and try running again.";
static SOME_CHECKS_FAILED_FATAL: &str = "Some checks failed :(. Sorry!";
static EVERYTING_CHECKS_OUT: &str =
    "Everything checks out! Continue at https://fuchsia.dev/fuchsia-src/get-started.";

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

    let re = Regex::new(r"(\d+)\.(\d+)\.\d+")?;
    let caps = re.captures(&stdout).ok_or(anyhow!("unexpected output from `defaults read`"))?;
    let major: u32 = caps.get(1).unwrap().as_str().parse()?;
    let minor: u32 = caps.get(2).unwrap().as_str().to_string().parse()?;
    Ok(OperatingSystem::MacOS(major, minor))
}

#[ffx_plugin()]
pub async fn preflight_cmd(_cmd: PreflightCommand) -> Result<()> {
    let config = PreflightConfig { system: get_operating_system()? };
    let checks: Vec<Box<dyn PreflightCheck>> = vec![
        Box::new(check::build_prereqs::BuildPrereqs::new(&command_runner::SYSTEM_COMMAND_RUNNER)),
        Box::new(check::femu_graphics::FemuGraphics::new(&command_runner::SYSTEM_COMMAND_RUNNER)),
        Box::new(check::emu_networking::EmuNetworking::new(&command_runner::SYSTEM_COMMAND_RUNNER)),
    ];

    run_preflight_checks(&mut stdout(), &checks, &config).await
}

async fn run_preflight_checks<W: Write>(
    writer: &mut W,
    checks: &Vec<Box<dyn PreflightCheck>>,
    config: &PreflightConfig,
) -> Result<()> {
    writeln!(writer, "{}", RUNNING_CHECKS_PREAMBLE)?;
    writeln!(writer)?;
    // Run the checks, and keep track of failures.
    let mut failures = vec![];
    for check in checks {
        let result = check.run(&config).await?;
        writeln!(writer, "{}", result)?;
        if matches!(result, PreflightCheckResult::Failure(..)) {
            failures.push(result);
        }
    }

    if !failures.is_empty() {
        // Collect the failures that are recoverable. If all failures are recoverable,
        // tell the user to try again after resolving them.
        let recoverable_failures: Vec<&PreflightCheckResult> = failures
            .iter()
            .take_while(|f| match *f {
                PreflightCheckResult::Failure(_, message) => match message {
                    Some(_) => true,
                    None => false,
                },
                _ => false,
            })
            .collect();
        if recoverable_failures.len() == failures.len() {
            ffx_bail!("{}", SOME_CHECKS_FAILED_RECOVERABLE);
        } else {
            ffx_bail!("{}", SOME_CHECKS_FAILED_FATAL);
        }
    } else {
        writeln!(writer, "{}", EVERYTING_CHECKS_OUT)?;
    }
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
        let run_command: command_runner::CommandRunner = |args| {
            assert_eq!(
                args.to_vec(),
                vec!["defaults", "read", "loginwindow", "SystemVersionStampAsString"]
            );
            Ok((ExitStatus(0), "10.15.17\n\n".to_string(), "".to_string()))
        };

        assert_eq!(OperatingSystem::MacOS(10, 15), get_operating_system_macos(&run_command)?);
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
        let result = run_preflight_checks(&mut buf, &checks, &config).await;
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
        let result = run_preflight_checks(&mut buf, &checks, &config).await;
        let output = String::from_utf8(buf)?;
        // Check for the various output strings.
        assert!(output.starts_with(RUNNING_CHECKS_PREAMBLE), output);
        assert!(output.contains("This check passed!"), output);
        assert!(output.contains("Oh no..."), output);
        match result {
            Err(error) => {
                assert!(error.to_string().contains(SOME_CHECKS_FAILED_FATAL), error.to_string());
                assert!(
                    !error.to_string().contains(SOME_CHECKS_FAILED_RECOVERABLE),
                    error.to_string()
                );
                Ok(())
            }
            Ok(()) => unreachable!(),
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
        let result = run_preflight_checks(&mut buf, &checks, &config).await;
        let output = String::from_utf8(buf)?;
        // Check for the various output strings.
        assert!(output.starts_with(RUNNING_CHECKS_PREAMBLE), output);
        assert!(output.contains("This check passed!"), output);
        assert!(output.contains("We will get through this."), output);
        assert!(output.contains("Take a deep breath and try again."), output);
        match result {
            Err(error) => {
                assert!(!error.to_string().contains(SOME_CHECKS_FAILED_FATAL), error.to_string());
                assert!(
                    error.to_string().contains(SOME_CHECKS_FAILED_RECOVERABLE),
                    error.to_string()
                );
                Ok(())
            }
            Ok(()) => unreachable!(),
        }
    }
}
