// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::check::{PreflightCheck, PreflightCheckResult, PreflightCheckResult::*},
    crate::command_runner::CommandRunner,
    crate::config::*,
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    lazy_static::lazy_static,
    regex::Regex,
};

lazy_static! {
    // Regex to extract video cards on Linux. Matches output strings from `lspci` like:
    // 18:00.0 VGA compatible controller: NVIDIA Corporation GP107GL [Quadro P1000] (rev a1)
    static ref LINUX_GRAPHICS_CARDS_RE: Regex = Regex::new("(?m)^..:.... VGA compatible controller: (.+)$").unwrap();

    // Regex to match the supported video cards on Linux. Includes:
    // * NVIDIA Corporation Quadro-based cards
    // * Vulkan capable Intel graphics cards
    static ref LINUX_SUPPORTED_CARDS_RE: Regex = Regex::new(r"^(?:Intel .*(?:HD|UHD|Iris|Iris Pro|Iris Plus) Graphics P?\d{3} .*|Intel .*Iris Xe Graphics.*|NVIDIA.+Quadro.+)$").unwrap();

    // Regex to extract graphics cards on MacOS. Matches output strings from `system_profiler` like:
    // Chipset Model: Intel UHD Graphics 630
    // Chipset Model: Radeon Pro 555X
    static ref MACOS_GRAPHICS_CARDS_RE: Regex = Regex::new(r"(?m)^\s+Chipset Model: (.+)$").unwrap();

    // Regex to match supported graphics cards on MacOS. Examples:
    // Intel UHD Graphics 630
    // Intel Iris Plus Graphics 655
    // Radeon Pro 555X
    static ref MACOS_SUPPORTED_CARDS_RE: Regex = Regex::new(r"^(?:Intel (?:U?HD|Iris)|Radeon Pro).+$").unwrap();
}

static NVIDIA_REQ_DRIVER_VERSION: (u32, u32) = (440, 100);

macro_rules! NO_GRAPHICS_WARNING {
    () => {
        "Did not find tested and supported graphics acceleration hardware. Usage of \
the graphical Fuchsia emulator (`fx vdl start`) may fall back to a slow \
software renderer. The terminal emulator (`fx qemu`) is unaffected.\n\n\
Found the following chipsets: {chipsets}\n\n\
Only a small set of chipsets are officially supported: \
https://fuchsia.dev/fuchsia-src/get-started/set_up_femu#supported-hardware"
    };
}
pub struct FemuGraphics<'a> {
    command_runner: &'a CommandRunner,
}

pub fn linux_find_graphics_cards(command_runner: &CommandRunner) -> Result<Vec<String>> {
    let (status, stdout, stderr) = (command_runner)(&vec!["lspci"])?;
    if !status.success() {
        return Err(anyhow!(
            "Could not exec `lspci`: exited with code {}, stdout: {}, stderr: {}",
            status.code(),
            stdout,
            stderr
        ));
    }

    Ok(LINUX_GRAPHICS_CARDS_RE
        .captures_iter(stdout.as_str())
        .map(|c| c[1].to_string())
        .collect::<Vec<_>>())
}

pub fn macos_find_graphics_cards(command_runner: &CommandRunner) -> Result<Vec<String>> {
    let (status, stdout, stderr) =
        (command_runner)(&vec!["system_profiler", "SPDisplaysDataType"])?;
    if !status.success() {
        return Err(anyhow!(
                "Could not exec `system_profiler SPDisplaysDataType`: exited with code {}, stdout: {}, stderr: {}",
                status.code(),
                stdout,
                stderr
            ));
    }

    Ok(MACOS_GRAPHICS_CARDS_RE
        .captures_iter(stdout.as_str())
        .map(|c| c[1].to_string())
        .collect::<Vec<_>>())
}

impl<'a> FemuGraphics<'a> {
    pub fn new(command_runner: &'a CommandRunner) -> Self {
        FemuGraphics { command_runner }
    }

    fn linux_find_supported_graphics_cards(&self) -> Result<Vec<String>> {
        Ok(linux_find_graphics_cards(self.command_runner)?
            .into_iter()
            .filter(|e| LINUX_SUPPORTED_CARDS_RE.is_match(&e))
            .collect::<Vec<_>>())
    }

    fn linux_get_nvidia_driver_version(&self) -> Result<(u32, u32)> {
        let (status, stdout, stderr) = (self.command_runner)(&vec![
            "nvidia-smi",
            "--query-gpu=driver_version",
            "--format=csv,noheader",
        ])?;
        if status.success() {
            let parts = stdout.trim().split(".").collect::<Vec<_>>();
            Ok((
                parts[0]
                    .parse()
                    .with_context(|| format!("could not parse '{}' to int", parts[0]))?,
                parts[1]
                    .parse()
                    .with_context(|| format!("could not parse '{}' to int", parts[1]))?,
            ))
        } else {
            Err(anyhow!(
                "Could not run `nvidia-smi`: exit code {}, stderr: {}",
                status.code(),
                stderr
            ))
        }
    }

    async fn run_linux(&self) -> Result<PreflightCheckResult> {
        let cards = self.linux_find_supported_graphics_cards()?;

        if cards.is_empty() {
            return Ok(Warning(format!(
                NO_GRAPHICS_WARNING!(),
                chipsets = linux_find_graphics_cards(self.command_runner)?.join(", ")
            )));
        }

        let has_nvidia_card = cards.iter().any(|name| name.starts_with("NVIDIA"));
        if has_nvidia_card {
            Ok(match self.linux_get_nvidia_driver_version() {
                Ok(driver_version) => {
                    if driver_version >= NVIDIA_REQ_DRIVER_VERSION {
                        Success(format!("Found supported graphics hardware: {}", cards.join(", ")))
                    } else {
                        Warning(format!("Found supported graphics hardware but nVidia driver version {}.{} is older than required version {}.{}. Download the required version at https://www.nvidia.com/download/driverResults.aspx/160175",
                            driver_version.0, driver_version.1, NVIDIA_REQ_DRIVER_VERSION.0, NVIDIA_REQ_DRIVER_VERSION.1))
                    }
                }
                Err(e) => Warning(format!(
                    "Found supported graphics hardware: {}; nVidia hardware couldn't be detected: {}",
                    cards.join(", "),
                    e
                )),
            })
        } else {
            Ok(Success(format!("Found supported graphics hardware: {}", cards.join(", "))))
        }
    }

    fn macos_find_supported_graphics_cards(&self) -> Result<Vec<String>> {
        Ok(macos_find_graphics_cards(self.command_runner)?
            .into_iter()
            .filter(|e| MACOS_SUPPORTED_CARDS_RE.is_match(&e))
            .collect::<Vec<_>>())
    }

    async fn run_macos(&self) -> Result<PreflightCheckResult, anyhow::Error> {
        let cards = self.macos_find_supported_graphics_cards()?;

        Ok(if cards.is_empty() {
            Warning(format!(
                NO_GRAPHICS_WARNING!(),
                chipsets = macos_find_graphics_cards(self.command_runner)?.join(", ")
            ))
        } else {
            Success(format!("Found supported graphics hardware: {}", cards.join(", ")))
        })
    }
}

#[async_trait(?Send)]
impl PreflightCheck for FemuGraphics<'_> {
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

    // Contains a "VGA compatible controller" line with an NVIDIA Quadro chipset.
    static LSPCI_OUTPUT_GOOD: &str =
        "17:1e.3 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
17:1e.6 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
18:00.0 VGA compatible controller: NVIDIA Corporation GP107GL [Quadro P1000] (rev a1)
18:00.1 Audio device: NVIDIA Corporation GP107GL High Definition Audio Controller (rev a1)
3a:09.0 System peripheral: Intel Corporation Sky Lake-E Integrated Memory Controller (rev 04)";

    // Contains a "VGA compatible controller" line with an Intel HD Graphics chipset and no nVidia.
    static LSPCI_OUTPUT_GOOD_INTEL: &str =
        "00:00.0 Host bridge: Intel Corporation Xeon E3-1200 v6/7th Gen Core Processor Host Bridge/DRAM Registers (rev 05)
00:01.0 PCI bridge: Intel Corporation Xeon E3-1200 v5/E3-1500 v5/6th Gen Core Processor PCIe Controller (x16) (rev 05)
00:02.0 VGA compatible controller: Intel Corporation HD Graphics 630 (rev 04)
00:14.0 USB controller: Intel Corporation 200 Series/Z370 Chipset Family USB 3.0 xHCI Controller
00:14.2 Signal processing controller: Intel Corporation 200 Series PCH Thermal Subsystem
00:16.0 Communication controller: Intel Corporation 200 Series PCH CSME HECI #1";

    // Contains an unsupported chipset.
    static LSPCI_OUTPUT_BAD: &str =
        "17:1e.3 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
17:1e.6 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
18:00.0 VGA compatible controller: NVIDIA No-good-o (rev a1)
18:00.1 Audio device: NVIDIA Corporation GP107GL High Definition Audio Controller (rev a1)
3a:05.0 System peripheral: Intel Corporation Sky Lake-E VT-d (rev 04)
3a:09.0 System peripheral: Intel Corporation Sky Lake-E Integrated Memory Controller (rev 04)";

    // Contains a supported Mac OS graphics chipset (Intel UHD)
    static SYSTEM_PROFILER_OUTPUT_GOOD1: &str = "Graphics/Displays:

Intel UHD Graphics 630:

  Chipset Model: Intel UHD Graphics 630
  Type: GPU
  Bus: Built-In
  VRAM (Dynamic, Max): 1536 MB
  Vendor: Intel
  Device ID: 0x5927
  Revision ID: 0x0006
  Metal: Supported, feature set macOS GPUFamily2 v1
";

    // Contains another supported Mac OS graphics chipset (Intel Iris)
    static SYSTEM_PROFILER_OUTPUT_GOOD2: &str = "Graphics/Displays:
Intel Iris Plus Graphics 655:

  Chipset Model: Intel Iris Plus Graphics 655
  Type: GPU
  Bus: Built-In
  VRAM (Dynamic, Max): 1536 MB
  Vendor: Intel
  Device ID: 0x3ea5
  Revision ID: 0x0001
  Metal Family: Supported, Metal GPUFamily macOS 2
";

    // Contains another supported Mac OS graphics chipset (Radeon Pro)
    static SYSTEM_PROFILER_OUTPUT_GOOD3: &str = "Graphics/Displays:

Radeon Pro 555X:

  Chipset Model: Radeon Pro 555X
  Type: GPU
  Bus: Built-In
  VRAM (Dynamic, Max): 1536 MB
  Vendor: Intel
  Device ID: 0x5927
  Revision ID: 0x0006
  Metal: Supported, feature set macOS GPUFamily2 v1
";

    // Contains an unsupported graphics chipset.
    static SYSTEM_PROFILER_OUTPUT_BAD: &str = "Graphics/Displays:

NVIDIA Quadro K1200:

  Chipset Model: NVIDIA Quadro K1200
  Type: GPU
  Bus: PCIe
  Slot: Slot-2
  PCIe Lane Width: x16
  VRAM (Total): 4095 MB
  Vendor: NVIDIA (0x10de)
  Device ID: 0x13bc
  Revision ID: 0x00a2
  ROM Revision: VBIOS 82.07.7f.00.14
  Metal: Supported
";

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_graphics_cards_re() -> Result<()> {
        let supported = vec![
            "Intel Corporation HD Graphics P530 (rev NN)",
            "Intel Corporation HD Graphics 630 (rev 04)",
            "Intel Corporation UHD Graphics 620 (rev 07)",
            "NVIDIA Corporation GP107GL [Quadro P1000] (rev a1)",
            "Intel Corporation Iris Pro Graphics P555 (rev NN)",
            "Intel Corporation Iris Plus Graphics 645 (rev NN)",
        ];
        let supported_n = supported.len();
        assert_eq!(
            supported.into_iter().filter(|e| LINUX_SUPPORTED_CARDS_RE.is_match(&e)).count(),
            supported_n
        );

        let unsupported = vec![
            "Intel Corporation HD Graphics 6000 (rev NN)",
            "Intel Corporation Iris Pro Graphics 6200 (rev NN)",
            "Intel Corporation Iris Graphics 6100 (rev NN)",
        ];
        assert_eq!(
            unsupported.into_iter().filter(|e| LINUX_SUPPORTED_CARDS_RE.is_match(&e)).count(),
            0
        );
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_success() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lspci"] {
                return Ok((ExitStatus(0), LSPCI_OUTPUT_GOOD.to_string(), "".to_string()));
            }
            assert_eq!(
                args.to_vec(),
                vec!["nvidia-smi", "--query-gpu=driver_version", "--format=csv,noheader"]
            );
            Ok((ExitStatus(0), "440.100\n".to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_success_newer_driver_version() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lspci"] {
                return Ok((ExitStatus(0), LSPCI_OUTPUT_GOOD.to_string(), "".to_string()));
            }
            assert_eq!(
                args.to_vec(),
                vec!["nvidia-smi", "--query-gpu=driver_version", "--format=csv,noheader"]
            );
            Ok((ExitStatus(0), "440.200\n".to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_success_newer_driver_version_2() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lspci"] {
                return Ok((ExitStatus(0), LSPCI_OUTPUT_GOOD.to_string(), "".to_string()));
            }
            assert_eq!(
                args.to_vec(),
                vec!["nvidia-smi", "--query-gpu=driver_version", "--format=csv,noheader"]
            );
            Ok((ExitStatus(0), "480.000\n".to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_success_intel() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lspci"] {
                return Ok((ExitStatus(0), LSPCI_OUTPUT_GOOD_INTEL.to_string(), "".to_string()));
            }
            unreachable!();
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_no_good_cards() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lspci"] {
                return Ok((ExitStatus(0), LSPCI_OUTPUT_BAD.to_string(), "".to_string()));
            }
            unreachable!();
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_linux_bad_driver_version() -> Result<()> {
        let run_command: CommandRunner = |args| {
            if args.to_vec() == vec!["lspci"] {
                return Ok((ExitStatus(0), LSPCI_OUTPUT_GOOD.to_string(), "".to_string()));
            }
            // nvidia-smi result
            Ok((ExitStatus(0), "420.100\n".to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::Linux }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_success_intel_hd_found() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["system_profiler", "SPDisplaysDataType"]);
            Ok((ExitStatus(0), SYSTEM_PROFILER_OUTPUT_GOOD1.to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_success_intel_iris_found() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["system_profiler", "SPDisplaysDataType"]);
            Ok((ExitStatus(0), SYSTEM_PROFILER_OUTPUT_GOOD2.to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_success_radeon_pro_found() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["system_profiler", "SPDisplaysDataType"]);
            Ok((ExitStatus(0), SYSTEM_PROFILER_OUTPUT_GOOD3.to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Success(..)));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_macos_no_good_cards() -> Result<()> {
        let run_command: CommandRunner = |args| {
            assert_eq!(args.to_vec(), vec!["system_profiler", "SPDisplaysDataType"]);
            Ok((ExitStatus(0), SYSTEM_PROFILER_OUTPUT_BAD.to_string(), "".to_string()))
        };

        let check = FemuGraphics::new(&run_command);
        let response = check.run(&PreflightConfig { system: OperatingSystem::MacOS(10, 15) }).await;
        assert!(matches!(response?, PreflightCheckResult::Warning(..)));
        Ok(())
    }
}
