// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::check::{PreflightCheck, PreflightCheckResult, PreflightCheckResult::*},
    crate::command_runner::CommandRunner,
    crate::config::*,
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
};

const LINUX_PACKAGES: &[&str] = &["curl", "git", "unzip"];
const MACOS_MINIMUM_MAJOR_VERSION: u32 = 10;
const MACOS_MINIMUM_MINOR_VERSION: u32 = 15;

pub struct BuildPrereqs<'a> {
    command_runner: &'a CommandRunner,
}

impl<'a> BuildPrereqs<'a> {
    pub fn new(command_runner: &'a CommandRunner) -> Self {
        BuildPrereqs { command_runner }
    }

    async fn run_linux(&self) -> Result<PreflightCheckResult> {
        // Ensure we're not on ARM.
        if self.did_find_linux_arm()? {
            return Ok(PreflightCheckResult::Failure(
                "ARM architectures are not supported at this time".to_string(),
                None,
            ));
        }

        // Ensure on a debian-flavor distribution.
        let (dpkg_status, _, __) = (self.command_runner)(&vec!["which", "dpkg"])?;
        if !dpkg_status.success() {
            return Ok(PreflightCheckResult::Warning(
                "Non-Debian linux distributions are not officially supported. Other preflight checks may or may not work, but you can continue on if you're adventurous".to_string()
            ));
        }

        let mut missing = vec![];
        for package in LINUX_PACKAGES {
            let (status, _stdout, __stderr) = (self.command_runner)(&vec!["dpkg", "-s", *package])
                .with_context(|| format!("Failed to query for package '{}'", package))?;
            if !status.success() {
                missing.push(*package);
            }
        }
        if !missing.is_empty() {
            return Ok(Failure(
                format!("Some build dependencies are missing: {}", missing.join(", ")),
                Some(format!("To resolve, run: sudo apt-get install {}", missing.join(" "))),
            ));
        }
        Ok(Success(format!("Found all needed build dependencies: {}", LINUX_PACKAGES.join(", "))))
    }

    fn did_find_linux_arm(&self) -> Result<bool, anyhow::Error> {
        let (status, stdout, stderr) =
            (self.command_runner)(&vec!["uname", "-m"]).context("Failed to run uname -m")?;
        if !status.success() {
            return Err(anyhow!("uname -m exited with {}: {}", status.code(), stderr));
        }
        Ok(stdout.starts_with("arm"))
    }

    async fn run_macos(
        &self,
        major_version: &u32,
        minor_version: &u32,
    ) -> Result<PreflightCheckResult, anyhow::Error> {
        if *major_version < MACOS_MINIMUM_MAJOR_VERSION
            || (*major_version == MACOS_MINIMUM_MAJOR_VERSION
                && *minor_version < MACOS_MINIMUM_MINOR_VERSION)
        {
            return Ok(Failure(
                format!(
                    "MacOS version {}.{} is less than the minimum required version {}.{}",
                    major_version,
                    minor_version,
                    MACOS_MINIMUM_MAJOR_VERSION,
                    MACOS_MINIMUM_MINOR_VERSION
                ),
                None,
            ));
        }

        // Check for Apple Silicon.
        if self.did_find_apple_silicon()? {
            return Ok(Warning("Apple Silicon is not officially supported. Unofficially, you can build from a terminal started with `arch -x86_64 /bin/zsh`.".to_string()));
        }

        let (status, stdout, _stderr) = (self.command_runner)(&vec!["xcode-select", "-p"])
            .context("Failed to run xcode-select")?;
        Ok(if status.success() {
            Success(format!("Xcode command line tools found at {}", stdout.trim()))
        } else {
            Failure(
                "Xcode command line tools not installed.".to_string(),
                Some(
                    "Download Xcode from the App Store and run `xcode-select --install`."
                        .to_string(),
                ),
            )
        })
    }

    fn did_find_apple_silicon(&self) -> Result<bool, anyhow::Error> {
        {
            let (status, stdout, stderr) =
                (self.command_runner)(&vec!["uname", "-p"]).context("Failed to run uname -p")?;
            if !status.success() {
                return Err(anyhow!("uname -p exited with {}: {}", status.code(), stderr));
            }
            if stdout == "arm" {
                return Ok(true);
            }
        }

        let (status, stdout, stderr) =
            (self.command_runner)(&vec!["sysctl", "-in", "sysctl.proc_translated"])
                .context("Failed to run sysctl")?;
        if !status.success() {
            return Err(anyhow!(
                "sysctl -in sysctl.proc_translated exited with {}: {}",
                status.code(),
                stderr
            ));
        }
        if stdout == "1" {
            // The process is running under Rosetta 2
            return Ok(true);
        }
        Ok(false)
    }
}

#[async_trait(?Send)]
impl PreflightCheck for BuildPrereqs<'_> {
    /// Returns Ok() if all build prerequisites are present on the system.
    async fn run(&self, config: &PreflightConfig) -> Result<PreflightCheckResult> {
        match &config.system {
            OperatingSystem::Linux => self.run_linux().await,
            OperatingSystem::MacOS(major_version, minor_version) => {
                self.run_macos(major_version, minor_version).await
            }
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::command_runner::ExitStatus};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success_linux() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["uname", "-m"] {
                return Ok((ExitStatus(0), "x86_64".to_string(), "".to_string()));
            }
            if args.to_vec() == vec!["which", "dpkg"] {
                return Ok((ExitStatus(0), "".to_string(), "".to_string()));
            }
            assert_eq!(args[0..2].to_vec(), vec!["dpkg", "-s"]);
            Ok((ExitStatus(0), "".to_string(), "".to_string()))
        };

        let check = BuildPrereqs::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_arm_linux() -> Result<()> {
        // Return "arm64" from uname -m, which should cause a Failure().
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["uname", "-m"] {
                return Ok((ExitStatus(0), "arm64".to_string(), "".to_string()));
            }
            unreachable!();
        };

        let check = BuildPrereqs::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Failure(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_non_debian_linux() -> Result<()> {
        // Fail one of the queries for a package.
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["uname", "-m"] {
                return Ok((ExitStatus(0), "x86_64".to_string(), "".to_string()));
            }
            if args.to_vec() == vec!["which", "dpkg"] {
                return Ok((ExitStatus(1), "".to_string(), "".to_string()));
            }
            unreachable!();
        };

        let check = BuildPrereqs::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_missing_package_linux() -> Result<()> {
        // Fail one of the queries for a package.
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["uname", "-m"] {
                return Ok((ExitStatus(0), "x86_64".to_string(), "".to_string()));
            }
            if args.to_vec() == vec!["which", "dpkg"] {
                return Ok((ExitStatus(0), "".to_string(), "".to_string()));
            }
            assert_eq!(args[0..2].to_vec(), vec!["dpkg", "-s"]);
            Ok(if args[2] == "git" {
                (ExitStatus(1), "".to_string(), "".to_string())
            } else {
                (ExitStatus(0), "".to_string(), "".to_string())
            })
        };

        let check = BuildPrereqs::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Failure(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_success_macos_10_15_and_newer() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["uname", "-p"] {
                return Ok((ExitStatus(0), "i386".to_string(), "".to_string()));
            }
            if args[0] == "sysctl" {
                return Ok((ExitStatus(0), "0".to_string(), "".to_string()));
            }
            assert_eq!(args.to_vec(), vec!["xcode-select", "-p"]);
            Ok((ExitStatus(0), "".to_string(), "".to_string()))
        };

        let check = BuildPrereqs::new(&run_command);
        for minor_version in 15..17 {
            let response = check
                .run(&PreflightConfig { system: OperatingSystem::MacOS(10, minor_version) })
                .await;
            assert!(matches!(response?, PreflightCheckResult::Success(..)));
        }

        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(11, 1) }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_warning_macos_10_15_m1_chipset() -> Result<()> {
        {
            let run_command: CommandRunner = |args| {
                assert_eq!(args.to_vec(), vec!["uname", "-p"]);
                Ok((ExitStatus(0), "arm".to_string(), "".to_string()))
            };

            let check = BuildPrereqs::new(&run_command);
            let response =
                check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
            assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        }
        {
            let run_command: CommandRunner = |args| {
                if args.to_vec() == vec!["uname", "-p"] {
                    return Ok((ExitStatus(0), "i386".to_string(), "".to_string()));
                }
                assert_eq!(args.to_vec(), vec!["sysctl", "-in", "sysctl.proc_translated"]);
                Ok((ExitStatus(0), "1".to_string(), "".to_string()))
            };

            let check = BuildPrereqs::new(&run_command);
            let response =
                check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
            assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_failure_macos_10_15_and_older() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["uname", "-p"] {
                return Ok((ExitStatus(0), "i386".to_string(), "".to_string()));
            }
            if args[0] == "sysctl" {
                return Ok((ExitStatus(0), "0".to_string(), "".to_string()));
            }
            assert_eq!(args.to_vec(), vec!["xcode-select", "-p"]);
            Ok((ExitStatus(1), "".to_string(), "".to_string()))
        };

        {
            let check = BuildPrereqs::new(&run_command);
            let response =
                check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
            assert!(matches!(response?, PreflightCheckResult::Failure(..)));
        }
        {
            let check = BuildPrereqs::new(&run_command);
            let response =
                check.run(&PreflightConfig { system: OperatingSystem::MacOS(9, 17) }).await;
            assert!(matches!(response?, PreflightCheckResult::Failure(..)));
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_failure_old_macos() -> Result<()> {
        let run_command: CommandRunner = |_| {
            unreachable!();
        };

        let check = BuildPrereqs::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 14) }).await;
        assert!(matches!(response?, PreflightCheckResult::Failure(..)));
        Ok(())
    }
}
