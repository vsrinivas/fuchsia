// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

#[derive(Debug, PartialEq, Eq)]
pub enum Builtin {
    PluginList,
    PluginControllers,
    PluginCollectors,
    PluginLoad,
    PluginUnload,
    PluginSchedule,
    Clear,
    Exit,
    Help,
    History,
}

pub struct BuiltinCommand {
    pub program: Builtin,
    pub args: Vec<String>,
}

impl BuiltinCommand {
    pub fn parse(command: impl Into<String>) -> Option<Self> {
        let token_buf = command.into();
        let mut tokens: VecDeque<&str> = token_buf.split_whitespace().collect();
        if tokens.len() == 0 {
            return None;
        }
        let program = tokens.pop_front().unwrap();
        let args: Vec<String> = tokens.iter().map(|s| s.to_string()).collect();

        match program {
            "plugin.list" => Some(Self { program: Builtin::PluginList, args }),
            "plugin.controllers" => Some(Self { program: Builtin::PluginControllers, args }),
            "plugin.collectors" => Some(Self { program: Builtin::PluginCollectors, args }),
            "plugin.load" => Some(Self { program: Builtin::PluginLoad, args }),
            "plugin.unload" => Some(Self { program: Builtin::PluginUnload, args }),
            "plugin.schedule" => Some(Self { program: Builtin::PluginSchedule, args }),
            "clear" => Some(Self { program: Builtin::Clear, args }),
            "exit" => Some(Self { program: Builtin::Exit, args }),
            "help" => Some(Self { program: Builtin::Help, args }),
            "history" => Some(Self { program: Builtin::History, args }),
            _ => None,
        }
    }
    pub fn usage() {
        println!(
            "
Scrutiny:
  A Security auditing framework and toolset.

Builtin Commands:
  plugin.list        - Lists all registered plugins and their state.
  plugin.controllers - Lists all controllers and their state.
  plugin.collectors  - Lists all collectors and their state.
  plugin.load        - Loads a registered plugin.
  plugin.unload      - Unloads a loaded plugin.
  plugin.schedule    - Schedules all the loaded collectors to run.
  clear              - Clears the screen.
  exit               - Exits the program.
  help               - Prints this help message.
  history            - Prints the command history for the shell."
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_builtin_parse() {
        assert_eq!(BuiltinCommand::parse("plugin.list").unwrap().program, Builtin::PluginList);
        assert_eq!(
            BuiltinCommand::parse("plugin.controllers").unwrap().program,
            Builtin::PluginControllers
        );
        assert_eq!(
            BuiltinCommand::parse("plugin.collectors").unwrap().program,
            Builtin::PluginCollectors
        );
        assert_eq!(BuiltinCommand::parse("plugin.load").unwrap().program, Builtin::PluginLoad);
        assert_eq!(BuiltinCommand::parse("plugin.unload").unwrap().program, Builtin::PluginUnload);
        assert_eq!(
            BuiltinCommand::parse("plugin.schedule").unwrap().program,
            Builtin::PluginSchedule
        );
        assert_eq!(BuiltinCommand::parse("clear").unwrap().program, Builtin::Clear);
        assert_eq!(BuiltinCommand::parse("exit").unwrap().program, Builtin::Exit);
        assert_eq!(BuiltinCommand::parse("help").unwrap().program, Builtin::Help);
        assert_eq!(BuiltinCommand::parse("history").unwrap().program, Builtin::History);
        assert_eq!(BuiltinCommand::parse("AAAtestAAA").is_none(), true);
    }

    #[test]
    fn test_builtin_args() {
        assert_eq!(BuiltinCommand::parse("plugin.list foo").unwrap().args, vec!["foo"]);
        assert_eq!(BuiltinCommand::parse("plugin.list foo bar").unwrap().args, vec!["foo", "bar"]);
    }
}
