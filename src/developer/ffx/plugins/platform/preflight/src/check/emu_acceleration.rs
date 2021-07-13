// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::check::{PreflightCheck, PreflightCheckResult, PreflightCheckResult::*},
    crate::command_runner::CommandRunner,
    crate::config::*,
    anyhow::Result,
    async_trait::async_trait,
};

static NO_CPU_VIRT_MESSAGE: &str = "CPU does not support virtualization. This \
will prevent emulator acceleration from working with the Fuchsia emulator.";
static NO_KVM_MESSAGE: &str = "KVM is not enabled for the current user. This \
will prevent emulator acceleration from working with the Fuchsia emulator.";
static NO_KVM_RESOLUTION_MESSAGE: &str = "enable KVM for the current user by \
following the instructions here: \
https://fuchsia.dev/fuchsia-src/get-started/set_up_femu#enable-kvm";

static WARNING_MESSAGE_MACOS: &str = "Hypervisor Framework is not enabled. \
This will prevent emulator acceleration from working with the Fuchsia emulator.";
static SUCCESS_MESSAGE_LINUX: &str = "KVM is enabled for the current user";
static SUCCESS_MESSAGE_MACOS: &str = "Hypervisor Framework is enabled.";

pub struct EmuAcceleration<'a> {
    command_runner: &'a CommandRunner,
}

impl<'a> EmuAcceleration<'a> {
    pub fn new(command_runner: &'a CommandRunner) -> Self {
        EmuAcceleration { command_runner }
    }

    async fn run_linux(&self) -> Result<PreflightCheckResult> {
        let (status, _, _) =
            (self.command_runner)(&vec!["grep", "-qE", "'vmx|svm'", "/proc/cpuinfo"])?;
        if !status.success() {
            return Ok(Warning(NO_CPU_VIRT_MESSAGE.to_string()));
        }
        let (status, _, _) = (self.command_runner)(&vec!["test", "-r", "/dev/kvm"])?;
        if !status.success() {
            return Ok(Warning(format!(
                "{}. To resolve, {}",
                NO_KVM_MESSAGE, NO_KVM_RESOLUTION_MESSAGE
            )));
        }
        Ok(Success(SUCCESS_MESSAGE_LINUX.to_string()))
    }

    async fn run_macos(&self) -> Result<PreflightCheckResult> {
        let (status, stdout, _) = (self.command_runner)(&vec!["sysctl", "kern.hv_support"])?;
        if status.success() && stdout.trim() == "kern.hv_support: 1" {
            return Ok(Success(SUCCESS_MESSAGE_MACOS.to_string()));
        }
        Ok(Warning(WARNING_MESSAGE_MACOS.to_string()))
    }
}

#[async_trait(?Send)]
impl PreflightCheck for EmuAcceleration<'_> {
    async fn run(&self, config: &PreflightConfig) -> Result<PreflightCheckResult> {
        match &config.system {
            OperatingSystem::Linux => self.run_linux().await,
            OperatingSystem::MacOS(..) => self.run_macos().await,
        }
    }
}

#[cfg(test)]
mod test {
    use {super::*, crate::command_runner::ExitStatus};

    static KERN_HV_SUPPORTED: &str = "\nkern.hv_support: 1\n";
    static KERN_HV_UNSUPPORTED: &str = "kern.hv_support: 0";

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_cpu_unsupported() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["grep", "-qE", "'vmx|svm'", "/proc/cpuinfo"]);
            return Ok((ExitStatus(1), "".to_string(), "".to_string()));
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_kvm_not_enabled() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["grep", "-qE", "'vmx|svm'", "/proc/cpuinfo"] {
                return Ok((ExitStatus(0), "vmx\n".to_string(), "".to_string()));
            }
            assert_eq!(args.to_vec(), vec!["test", "-r", "/dev/kvm"]);
            Ok((ExitStatus(1), "".to_string(), "".to_string()))
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_success() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["grep", "-qE", "'vmx|svm'", "/proc/cpuinfo"] {
                return Ok((ExitStatus(0), "vmx\n".to_string(), "".to_string()));
            }
            assert_eq!(args.to_vec(), vec!["test", "-r", "/dev/kvm"]);
            Ok((ExitStatus(0), "".to_string(), "".to_string()))
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_command_fails() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["sysctl", "kern.hv_support"]);
            Ok((ExitStatus(1), "".to_string(), "sysctl: unknown oid 'kern.hv_support'".to_string()))
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_unsupported() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["sysctl", "kern.hv_support"]);
            Ok((ExitStatus(0), KERN_HV_UNSUPPORTED.to_string(), "".to_string()))
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_success() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["sysctl", "kern.hv_support"]);
            Ok((ExitStatus(0), KERN_HV_SUPPORTED.to_string(), "".to_string()))
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }
}
