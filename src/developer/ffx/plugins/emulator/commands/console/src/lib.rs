// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Result};
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_commands::get_engine_by_name;
use ffx_emulator_common::config::FfxConfigWrapper;
use ffx_emulator_config::EngineConsoleType;
use ffx_emulator_console_args::ConsoleCommand;

fn get_console_type(cmd: &ConsoleCommand) -> Result<EngineConsoleType> {
    let mut result = cmd.console_type;
    if cmd.serial {
        if result == EngineConsoleType::None || result == EngineConsoleType::Serial {
            result = EngineConsoleType::Serial;
        } else {
            return Err(anyhow!("Only one of --serial, --command, or --machine may be specified."));
        }
    }
    if cmd.command {
        if result == EngineConsoleType::None || result == EngineConsoleType::Command {
            result = EngineConsoleType::Command;
        } else {
            return Err(anyhow!("Only one of --serial, --command, or --machine may be specified."));
        }
    }
    if cmd.machine {
        if result == EngineConsoleType::None || result == EngineConsoleType::Machine {
            result = EngineConsoleType::Machine;
        } else {
            return Err(anyhow!("Only one of --serial, --command, or --machine may be specified."));
        }
    }
    if result == EngineConsoleType::None {
        result = EngineConsoleType::Serial;
    }
    Ok(result)
}

#[ffx_plugin("emu.console.enabled")]
pub async fn console(mut cmd: ConsoleCommand) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let console = match get_console_type(&cmd) {
        Ok(c) => c,
        Err(e) => ffx_bail!("{:?}", e),
    };
    match get_engine_by_name(&ffx_config, &mut cmd.name).await {
        Ok(engine) => engine.attach(console),
        Err(e) => ffx_bail!("{:?}", e),
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_get_console_type() -> Result<()> {
        let mut cmd = ConsoleCommand::default();

        // Nothing is selected, so it defaults to Serial.
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Serial);

        // Check each value of the --console_type flag
        cmd.console_type = EngineConsoleType::Command;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Command);

        cmd.console_type = EngineConsoleType::Machine;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Machine);

        cmd.console_type = EngineConsoleType::Serial;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Serial);

        // Check that each of the standalone flags work.
        cmd.console_type = EngineConsoleType::None;
        cmd.command = true;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Command);
        cmd.command = false;

        cmd.machine = true;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Machine);
        cmd.machine = false;

        cmd.serial = true;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Serial);
        cmd.serial = false;

        // Check that if the console_type is set, and the matching stand-alone is set, it's still ok
        cmd.command = true;
        cmd.console_type = EngineConsoleType::Command;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Command);
        cmd.command = false;

        cmd.machine = true;
        cmd.console_type = EngineConsoleType::Machine;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Machine);
        cmd.machine = false;

        cmd.serial = true;
        cmd.console_type = EngineConsoleType::Serial;
        let result = get_console_type(&cmd);
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), EngineConsoleType::Serial);
        cmd.serial = false;

        // Check that if any two standalones are set, or all three, it's an error
        cmd.command = true;
        cmd.serial = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.serial = false;
        cmd.command = false;

        cmd.command = true;
        cmd.machine = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.machine = false;
        cmd.command = false;

        cmd.machine = true;
        cmd.serial = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.machine = false;
        cmd.serial = false;

        cmd.command = true;
        cmd.machine = true;
        cmd.serial = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.machine = false;
        cmd.serial = false;
        cmd.command = false;

        // Check that if console_type is set, and also a non-matching standalone, it's an error
        cmd.console_type = EngineConsoleType::Serial;
        cmd.command = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.command = false;
        cmd.machine = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.machine = false;

        cmd.console_type = EngineConsoleType::Machine;
        cmd.command = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.command = false;
        cmd.serial = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.serial = false;

        cmd.console_type = EngineConsoleType::Command;
        cmd.serial = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.serial = false;
        cmd.machine = true;
        assert!(get_console_type(&cmd).is_err());
        cmd.machine = false;

        Ok(())
    }
}
