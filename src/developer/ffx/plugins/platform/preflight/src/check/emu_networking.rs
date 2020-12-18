// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::check::{PreflightCheck, PreflightCheckResult, PreflightCheckResult::*},
    crate::command_runner::CommandRunner,
    crate::config::*,
    anyhow::Result,
    async_trait::async_trait,
    lazy_static::lazy_static,
    regex::Regex,
};

static NO_TUNTAP_MESSAGE: &str =
    "Did not find a tuntap device named 'qemu' for the current user. This will prevent you from \
enabling network access for the emulator. You can see all existing devices by executing \
`ip tuntap show`";
static NO_TUNTAP_RESOLUTION_MESSAGE: &str = "\
run:

$ sudo ip tuntap add dev qemu mode tap user $USER
$ sudo ip link set qemu up";
static SUCCESS_MESSAGE_LINUX: &str = "Found tuntap device named 'qemu' for current user";
static SUCCESS_MESSAGE_MACOS: &str = "MacOS provides networking for the Fuchsia emulator natively";

lazy_static! {
    // Regex to match the output of `ip tuntap list` looking for a tuntap named "qemu".
    static ref TUNTAP_RE: Regex = Regex::new(r"qemu: tap persist user (\d+)").unwrap();
}

pub struct EmuNetworking<'a> {
    command_runner: &'a CommandRunner,
}

impl<'a> EmuNetworking<'a> {
    pub fn new(command_runner: &'a CommandRunner) -> Self {
        EmuNetworking { command_runner }
    }

    fn ensure_is_same_user(&self, id_string: &str) -> Result<PreflightCheckResult> {
        let (_status, stdout, _) = (self.command_runner)(&vec!["id", "-u"])?;
        Ok(if stdout.trim() == id_string.trim() {
            Success(SUCCESS_MESSAGE_LINUX.to_string())
        } else {
            Warning(format!("{}. To resolve, {}", NO_TUNTAP_MESSAGE, NO_TUNTAP_RESOLUTION_MESSAGE))
        })
    }

    async fn run_linux(&self) -> Result<PreflightCheckResult> {
        let (_status, stdout, _) = (self.command_runner)(&vec!["ip", "tuntap", "list"])?;

        let caps = TUNTAP_RE.captures(&stdout);
        match &caps {
            Some(c) => self.ensure_is_same_user(&c[1]),
            None => Ok(Warning(format!(
                "{}. To resolve, {}",
                NO_TUNTAP_MESSAGE, NO_TUNTAP_RESOLUTION_MESSAGE
            ))),
        }
    }
}

#[async_trait(?Send)]
impl PreflightCheck for EmuNetworking<'_> {
    async fn run(&self, config: &PreflightConfig) -> Result<PreflightCheckResult> {
        match &config.system {
            OperatingSystem::Linux => self.run_linux().await,
            OperatingSystem::MacOS(..) => Ok(Success(SUCCESS_MESSAGE_MACOS.to_string())),
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::command_runner::ExitStatus};

    static IP_TUNTAP_OUTPUT_FOUND: &str = "qemu: tap persist user 12345\n";
    static IP_TUNTAP_OUTPUT_NOT_FOUND: &str = "\n";

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_success() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["id", "-u"] {
                return Ok((ExitStatus(0), "12345\n".to_string(), "".to_string()));
            }
            assert_eq!(args.to_vec(), vec!["ip", "tuntap", "list"]);
            Ok((ExitStatus(0), IP_TUNTAP_OUTPUT_FOUND.to_string(), "".to_string()))
        };

        let check = EmuNetworking::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_no_tuntap_device() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["id", "-u"] {
                unreachable!();
            }
            assert_eq!(args.to_vec(), vec!["ip", "tuntap", "list"]);
            Ok((ExitStatus(0), IP_TUNTAP_OUTPUT_NOT_FOUND.to_string(), "".to_string()))
        };

        let check = EmuNetworking::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_tuntap_found_for_different_user() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["id", "-u"] {
                return Ok((ExitStatus(0), "54321\n".to_string(), "".to_string()));
            }
            assert_eq!(args.to_vec(), vec!["ip", "tuntap", "list"]);
            Ok((ExitStatus(0), IP_TUNTAP_OUTPUT_FOUND.to_string(), "".to_string()))
        };

        let check = EmuNetworking::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_success() -> Result<()> {
        let run_command: CommandRunner = |_args| {
            unreachable!();
        };

        let check = EmuNetworking::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }
}
