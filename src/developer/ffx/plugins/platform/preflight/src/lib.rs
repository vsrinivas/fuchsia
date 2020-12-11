// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    check::PreflightCheck,
    config::*,
    ffx_core::ffx_plugin,
    ffx_preflight_args::PreflightCommand,
    regex::Regex,
};

mod check;
mod command_runner;
mod config;

#[cfg(target_os = "linux")]
fn get_operating_system() -> Result<OperatingSystem> {
    Ok(OperatingSystem::Linux)
}

#[cfg(target_os = "macos")]
fn get_operating_system() -> Result<OperatingSystem> {
    let command_runner: command_runner::CommandRunner = command_runner::system_run_command;
    get_operating_system_macos(&command_runner)
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
    let command_runner: command_runner::CommandRunner = command_runner::system_run_command;
    let checks: Vec<Box<dyn PreflightCheck>> =
        vec![Box::new(check::build_prereqs::BuildPrereqs::new(&command_runner))];
    for check in checks {
        println!("{:?}", check.run(&config).await?);
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use {super::*, crate::command_runner::ExitStatus};

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
}
