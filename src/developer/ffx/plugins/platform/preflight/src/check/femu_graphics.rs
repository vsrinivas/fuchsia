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
    // Regex to match the only supported video card on Linux. Matches output strings from `lspci` like:
    // 18:00.0 VGA compatible controller: NVIDIA Corporation GP107GL [Quadro P1000] (rev a1)
    static ref NVIDIA_CARD_RE: Regex = Regex::new("(?m)^..:.... VGA compatible controller: (NVIDIA.+Quadro.+)$").unwrap();

    // Regex to find all supported graphics cards on MacOS. Matches output strings from `system_profiler` like:
    // Chipset Model: Intel UHD Graphics 630
    // Chipset Model: Radeon Pro 555X
    static ref MACOS_GRAPHICS_CARDS_RE: Regex = Regex::new(r"(?m)^\s+Chipset Model: ((?:Intel U?HD|Radeon Pro).+)$").unwrap();
}

static NVIDIA_REQ_DRIVER_VERSION: (u32, u32) = (440, 100);

static NO_GRAPHICS_WARNING_LINUX: &str =
    "Did not find any supported graphics acceleration hardware. Usage of \
the graphical Fuchsia emulator (`fx emu`) will fall back to a slow \
software renderer. The terminal emulator (`fx qemu`) is unaffected.";

static NO_GRAPHICS_WARNING_MACOS: &str =
    "Did not find any supported graphics acceleration hardware. Usage of \
the graphical Fuchsia emulator (`fx vdl --start`) will fall back to a slow \
software renderer. The terminal emulator (`fx qemu`) is unaffected.";

pub struct FemuGraphics<'a> {
    command_runner: &'a CommandRunner,
}

impl<'a> FemuGraphics<'a> {
    pub fn new(command_runner: &'a CommandRunner) -> Self {
        FemuGraphics { command_runner }
    }

    fn linux_find_graphics_cards(&self) -> Result<Vec<String>> {
        let (status, stdout, stderr) = (self.command_runner)(&vec!["lspci"])?;
        if !status.success() {
            return Err(anyhow!(
                "Could not exec `lspci`: exited with code {}, stdout: {}, stderr: {}",
                status.code(),
                stdout,
                stderr
            ));
        }

        Ok(NVIDIA_CARD_RE
            .captures_iter(stdout.as_str())
            .map(|c| c[1].to_string())
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
        let cards = self.linux_find_graphics_cards()?;

        if cards.is_empty() {
            return Ok(Warning(NO_GRAPHICS_WARNING_LINUX.to_string()));
        }

        let driver_version = self.linux_get_nvidia_driver_version()?;
        Ok(if driver_version == NVIDIA_REQ_DRIVER_VERSION {
            Success(format!("Found supported graphics hardware: {}", cards.join(", ")))
        } else {
            Warning(format!("Found supported graphics hardware but Nvidia driver version {}.{} does not match required version {}.{}. Download the required version at https://www.nvidia.com/download/driverResults.aspx/160175",
                driver_version.0, driver_version.1, NVIDIA_REQ_DRIVER_VERSION.0, NVIDIA_REQ_DRIVER_VERSION.1))
        })
    }

    fn macos_find_graphics_cards(&self) -> Result<Vec<String>> {
        let (status, stdout, stderr) =
            (self.command_runner)(&vec!["system_profiler", "SPDisplaysDataType"])?;
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

    async fn run_macos(&self) -> Result<PreflightCheckResult, anyhow::Error> {
        let cards = self.macos_find_graphics_cards()?;

        Ok(if cards.is_empty() {
            Warning(NO_GRAPHICS_WARNING_MACOS.to_string())
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
17:1e.4 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
17:1e.5 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
17:1e.6 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
18:00.0 VGA compatible controller: NVIDIA Corporation GP107GL [Quadro P1000] (rev a1)
18:00.1 Audio device: NVIDIA Corporation GP107GL High Definition Audio Controller (rev a1)
3a:05.0 System peripheral: Intel Corporation Sky Lake-E VT-d (rev 04)
3a:05.2 System peripheral: Intel Corporation Sky Lake-E RAS Configuration Registers (rev 04)
3a:05.4 PIC: Intel Corporation Sky Lake-E IOxAPIC Configuration Registers (rev 04)
3a:08.0 System peripheral: Intel Corporation Sky Lake-E Integrated Memory Controller (rev 04)
3a:09.0 System peripheral: Intel Corporation Sky Lake-E Integrated Memory Controller (rev 04)";

    // Contains an unsupported chipset.
    static LSPCI_OUTPUT_BAD: &str =
        "17:1e.3 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
17:1e.4 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
17:1e.5 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
17:1e.6 System peripheral: Intel Corporation Sky Lake-E PCU Registers (rev 04)
18:00.0 VGA compatible controller: NVIDIA No-good-o (rev a1)
18:00.1 Audio device: NVIDIA Corporation GP107GL High Definition Audio Controller (rev a1)
3a:05.0 System peripheral: Intel Corporation Sky Lake-E VT-d (rev 04)
3a:05.2 System peripheral: Intel Corporation Sky Lake-E RAS Configuration Registers (rev 04)
3a:05.4 PIC: Intel Corporation Sky Lake-E IOxAPIC Configuration Registers (rev 04)
3a:08.0 System peripheral: Intel Corporation Sky Lake-E Integrated Memory Controller (rev 04)
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

    // Contains another supported Mac OS graphics chipset (Radeon Pro)
    static SYSTEM_PROFILER_OUTPUT_GOOD2: &str = "Graphics/Displays:

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

Intel Iris Plus Graphics 650:

  Chipset Model: Intel Iris Plus Graphics 650
  Type: GPU
  Bus: Built-In
  VRAM (Dynamic, Max): 1536 MB
  Vendor: Intel
  Device ID: 0x5927
  Revision ID: 0x0006
  Metal: Supported, feature set macOS GPUFamily2 v1
";

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
    async fn test_macos_success_radeon_pro_found() -> Result<()> {
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
