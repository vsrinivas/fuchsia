// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::VecDeque;

#[derive(Debug, PartialEq, Eq)]
pub enum Builtin {
    PluginLoad,
    PluginUnload,
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
            "plugin.load" => Some(Self { program: Builtin::PluginLoad, args }),
            "plugin.unload" => Some(Self { program: Builtin::PluginUnload, args }),
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
  plugin.load        - Loads a registered plugin.
  plugin.unload      - Unloads a loaded plugin.
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
        assert_eq!(BuiltinCommand::parse("plugin.load").unwrap().program, Builtin::PluginLoad);
        assert_eq!(BuiltinCommand::parse("plugin.unload").unwrap().program, Builtin::PluginUnload);
        assert_eq!(BuiltinCommand::parse("clear").unwrap().program, Builtin::Clear);
        assert_eq!(BuiltinCommand::parse("exit").unwrap().program, Builtin::Exit);
        assert_eq!(BuiltinCommand::parse("help").unwrap().program, Builtin::Help);
        assert_eq!(BuiltinCommand::parse("history").unwrap().program, Builtin::History);
        assert_eq!(BuiltinCommand::parse("AAAtestAAA").is_none(), true);
    }

    #[test]
    fn test_builtin_args() {
        assert_eq!(BuiltinCommand::parse("plugin.load foo").unwrap().args, vec!["foo"]);
        assert_eq!(BuiltinCommand::parse("plugin.load foo bar").unwrap().args, vec!["foo", "bar"]);
    }
}
