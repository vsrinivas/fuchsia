// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::check::{PreflightCheck, PreflightCheckResult, PreflightCheckResult::*},
    crate::command_runner::CommandRunner,
    crate::config::*,
    anyhow::{anyhow, Result},
    async_trait::async_trait,
    lazy_static::lazy_static,
    regex::Regex,
};

lazy_static! {
    // Regex to check whether the output of `lscpu` contains either VT-x or AMD-V to confirm that the CPU supports virtualization.
    static ref CPU_VIRTUALIZATION_RE: Regex = Regex::new(r"Virtualization:(\s)*(VT-x|AMD-V).*").unwrap();
}

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

pub fn linux_check_cpu_virtualization(command_runner: &CommandRunner) -> Result<bool> {
    let (status, stdout, stderr) = (command_runner)(&vec!["lscpu"])?;
    if !status.success() {
        return Err(anyhow!(
            "Could not exec `lscpu`: exited with code {}, stdout: {}, stderr: {}",
            status.code(),
            stdout,
            stderr
        ));
    }

    Ok(CPU_VIRTUALIZATION_RE.is_match(stdout.as_str()))
}

impl<'a> EmuAcceleration<'a> {
    pub fn new(command_runner: &'a CommandRunner) -> Self {
        EmuAcceleration { command_runner }
    }

    async fn run_linux(&self) -> Result<PreflightCheckResult> {
        let supports_virtualization = linux_check_cpu_virtualization(self.command_runner)?;
        if !supports_virtualization {
            return Ok(Warning(NO_CPU_VIRT_MESSAGE.to_string()));
        }
        let (status, _, _) = (self.command_runner)(&vec!["test", "-r", "/dev/kvm"])?;
        if !status.success() {
            return Ok(Warning(format!(
                "{} To resolve, {}",
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

    static LSCPU_OUTPUT_GOOD_INTEL: &str = "Architecture:                    x86_64
    CPU op-mode(s):                  32-bit, 64-bit
    Byte Order:                      Little Endian
    Address sizes:                   46 bits physical, 48 bits virtual
    CPU(s):                          12
    On-line CPU(s) list:             0-11
    Thread(s) per core:              2
    Core(s) per socket:              6
    Socket(s):                       1
    NUMA node(s):                    1
    Vendor ID:                       GenuineIntel
    CPU family:                      6
    Model:                           85
    Model name:                      Intel(R) Xeon(R) W-2135 CPU @ 3.70GHz
    Stepping:                        4
    CPU MHz:                         1223.575
    CPU max MHz:                     4500.0000
    CPU min MHz:                     1200.0000
    BogoMIPS:                        7399.70
    Virtualization:                  VT-x
    L1d cache:                       192 KiB
    L1i cache:                       192 KiB
    L2 cache:                        6 MiB
    L3 cache:                        8.3 MiB
    NUMA node0 CPU(s):               0-11";
    static LSCPU_OUTPUT_GOOD_AMD: &str = "Architecture:                    x86_64
    CPU op-mode(s):                  32-bit, 64-bit
    Byte Order:                      Little Endian
    Address sizes:                   43 bits physical, 48 bits virtual
    CPU(s):                          128
    On-line CPU(s) list:             0-127
    Thread(s) per core:              2
    Core(s) per socket:              64
    Socket(s):                       1
    NUMA node(s):                    1
    Vendor ID:                       AuthenticAMD
    CPU family:                      6
    Model:                           85
    Model name:                      AMD EPYC 7V12 64-Core Processor
    Stepping:                        4
    CPU MHz:                         1500.061
    CPU max MHz:                     2450.0000
    CPU min MHz:                     1500.0000
    BogoMIPS:                        4900.44
    Virtualization:                  AMD-V
    L1d cache:                       2 MiB
    L1i cache:                       2 MiB
    L2 cache:                        32 MiB
    L3 cache:                        256 MiB
    NUMA node0 CPU(s):               0-127";
    static LSCPU_OUTPUT_GOOD_BAD: &str = "Architecture:                    x86_64
    CPU op-mode(s):                  32-bit, 64-bit
    Byte Order:                      Little Endian
    Address sizes:                   43 bits physical, 48 bits virtual
    CPU(s):                          128
    On-line CPU(s) list:             0-127
    Thread(s) per core:              2
    Core(s) per socket:              64
    Socket(s):                       1
    NUMA node(s):                    1
    Vendor ID:                       AuthenticAMD
    CPU family:                      6
    Model:                           85
    Model name:                      AMD EPYC 7V12 64-Core Processor
    Stepping:                        4
    CPU MHz:                         1500.061
    CPU max MHz:                     2450.0000
    CPU min MHz:                     1500.0000
    BogoMIPS:                        4900.44
    Virtualization:                  none
    L1d cache:                       2 MiB
    L1i cache:                       2 MiB
    L2 cache:                        32 MiB
    L3 cache:                        256 MiB
    NUMA node0 CPU(s):               0-127";
    static KERN_HV_SUPPORTED: &str = "\nkern.hv_support: 1\n";
    static KERN_HV_UNSUPPORTED: &str = "kern.hv_support: 0";

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_cpu_unsupported() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["lscpu"]);
            return Ok((ExitStatus(0), LSCPU_OUTPUT_GOOD_BAD.to_string(), "".to_string()));
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_kvm_not_enabled() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lscpu"] {
                return Ok((ExitStatus(0), LSCPU_OUTPUT_GOOD_INTEL.to_string(), "".to_string()));
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
    async fn test_linux_success_intel() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lscpu"] {
                return Ok((ExitStatus(0), LSCPU_OUTPUT_GOOD_INTEL.to_string(), "".to_string()));
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
    async fn test_linux_success_amd() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lscpu"] {
                return Ok((ExitStatus(0), LSCPU_OUTPUT_GOOD_AMD.to_string(), "".to_string()));
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
