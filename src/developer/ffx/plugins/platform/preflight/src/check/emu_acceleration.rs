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
    // Regex to check whether /proc/cpuinfo indicates either VT-x or AMD-V virtualization extensions.
    static ref CPU_VIRTUALIZATION_RE: Regex = Regex::new(r"(?m)^flags\s*:.*\b(vmx|svm)\b").unwrap();
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
    let (status, stdout, stderr) = (command_runner)(&vec!["cat", "/proc/cpuinfo"])?;
    if !status.success() {
        return Err(anyhow!(
            "Could not exec `cat /proc/cpuinfo`: exited with code {}, stdout: {}, stderr: {}",
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

    static CPUINFO_OUTPUT_GOOD_INTEL: &str = "processor	: 0
vendor_id	: GenuineIntel
cpu family	: 6
model		: 142
model name	: Intel(R) Core(TM) i7-8650U CPU @ 1.90GHz
stepping	: 10
microcode	: 0xea
cpu MHz		: 2100.000
cache size	: 8192 KB
physical id	: 0
siblings	: 8
core id		: 0
cpu cores	: 4
apicid		: 0
initial apicid	: 0
fpu		: yes
fpu_exception	: yes
cpuid level	: 22
wp		: yes
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm constant_tsc art arch_perfmon pebs bts rep_good nopl xtopology nonstop_tsc cpuid aperfmperf pni pclmulqdq dtes64 monitor ds_cpl vmx smx est tm2 ssse3 sdbg fma cx16 xtpr pdcm pcid sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand lahf_lm abm 3dnowprefetch cpuid_fault epb invpcid_single pti ssbd ibrs ibpb stibp tpr_shadow vnmi flexpriority ept vpid ept_ad fsgsbase tsc_adjust bmi1 avx2 smep bmi2 erms invpcid mpx rdseed adx smap clflushopt intel_pt xsaveopt xsavec xgetbv1 xsaves dtherm ida arat pln pts hwp hwp_notify hwp_act_window hwp_epp md_clear flush_l1d
vmx flags	: vnmi preemption_timer invvpid ept_x_only ept_ad ept_1gb flexpriority tsc_offset vtpr mtf vapic ept vpid unrestricted_guest ple shadow_vmcs pml ept_mode_based_exec
bugs		: cpu_meltdown spectre_v1 spectre_v2 spec_store_bypass l1tf mds swapgs taa itlb_multihit srbds
bogomips	: 4199.88
clflush size	: 64
cache_alignment	: 64
address sizes	: 39 bits physical, 48 bits virtual
power management:";

    static CPUINFO_OUTPUT_GOOD_AMD: &str = "processor       : 0
vendor_id       : AuthenticAMD
cpu family      : 23
model           : 1
model name      : AMD EPYC 7601 32-Core Processor
stepping        : 2
microcode       : 0x8001206
cpu MHz         : 1200.000
cache size      : 512 KB
physical id     : 1
siblings        : 64
core id         : 7
cpu cores       : 32
apicid          : 127
initial apicid  : 127
fpu             : yes
fpu_exception   : yes
cpuid level     : 13
wp              : yes
flags           : fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 ht syscall nx mmxext fxsr_opt pdpe1gb rdtscp lm constant_tsc rep_good nopl nonstop_tsc extd_apicid amd_dcm aperfmperf eagerfpu pni pclmulqdq monitor ssse3 fma cx16 sse4_1 sse4_2 movbe popcnt aes xsave avx f16c rdrand lahf_lm cmp_legacy svm extapic cr8_legacy abm sse4a misalignsse 3dnowprefetch osvw skinit wdt tce topoext perfctr_core perfctr_nb bpext perfctr_l2 mwaitx cpb hw_pstate vmmcall fsgsbase bmi1 avx2 smep bmi2 rdseed adx smap clflushopt sha_ni xsaveopt xsavec xgetbv1 xsaves clzero irperf arat npt lbrv svm_lock nrip_save tsc_scale vmcb_clean flushbyasid decodeassists pausefilter pfthreshold avic overflow_recov succor smca
bugs            : fxsave_leak sysret_ss_attrs null_seg
bogomips        : 4399.27
TLB size        : 2560 4K pages
clflush size    : 64
cache_alignment : 64
address sizes   : 48 bits physical, 48 bits virtual
power management: ts ttp tm hwpstate cpb eff_freq_ro [13] [14]";
    static CPUINFO_OUTPUT_GOOD_BAD: &str = "processor	: 0
vendor_id	: AuthenticAMD
cpu family	: 23
model		: 1
model name	: AMD EPYC 7601 32-Core Processor
stepping	: 2
microcode	: 0x1000065
cpu MHz		: 2200.000
cache size	: 512 KB
physical id	: 0
siblings	: 1
core id		: 0
cpu cores	: 1
apicid		: 0
initial apicid	: 0
fpu		: yes
fpu_exception	: yes
cpuid level	: 13
wp		: yes
flags		: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush mmx fxsr sse sse2 syscall nx mmxext fxsr_opt pdpe1gb rdtscp lm rep_good nopl cpuid extd_apicid tsc_known_freq pni pclmulqdq ssse3 fma cx16 sse4_1 sse4_2 x2apic movbe popcnt tsc_deadline_timer aes xsave avx f16c rdrand hypervisor lahf_lm cmp_legacy cr8_legacy abm sse4a misalignsse 3dnowprefetch osvw perfctr_core ssbd ibpb vmmcall fsgsbase tsc_adjust bmi1 avx2 smep bmi2 rdseed adx smap clflushopt sha_ni xsaveopt xsavec xgetbv1 virt_ssbd arat arch_capabilities
bugs		: fxsave_leak sysret_ss_attrs null_seg spectre_v1 spectre_v2 spec_store_bypass
bogomips	: 4401.33
TLB size	: 1024 4K pages
clflush size	: 64
cache_alignment	: 64
address sizes	: 40 bits physical, 48 bits virtual
power management:";
    static KERN_HV_SUPPORTED: &str = "\nkern.hv_support: 1\n";
    static KERN_HV_UNSUPPORTED: &str = "kern.hv_support: 0";

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_cpu_unsupported() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["cat", "/proc/cpuinfo"]);
            return Ok((ExitStatus(0), CPUINFO_OUTPUT_GOOD_BAD.to_string(), "".to_string()));
        };

        let check = EmuAcceleration::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_kvm_not_enabled() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["cat", "/proc/cpuinfo"] {
                return Ok((ExitStatus(0), CPUINFO_OUTPUT_GOOD_INTEL.to_string(), "".to_string()));
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
            if args.to_vec() == vec!["cat", "/proc/cpuinfo"] {
                return Ok((ExitStatus(0), CPUINFO_OUTPUT_GOOD_INTEL.to_string(), "".to_string()));
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
            if args.to_vec() == vec!["cat", "/proc/cpuinfo"] {
                return Ok((ExitStatus(0), CPUINFO_OUTPUT_GOOD_AMD.to_string(), "".to_string()));
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
