// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    rustyline::{
        completion::Completer, error::ReadlineError, highlight::Highlighter, hint::Hinter, Helper,
    },
    std::{
        borrow::Cow::{self, Borrowed, Owned},
        fmt,
        str::FromStr,
    },
};

/// Macro to generate a command enum and its impl.
/// A command consists of the command name, optional flags, arguments, and a help description.
macro_rules! gen_commands {
    ($name:ident {
        $($variant:ident = ($val:expr, [$($flag:expr),*], [$($arg:expr),*], $help:expr)),*,
    }) => {
        /// Enum of all possible commands
        #[derive(PartialEq)]
        pub enum $name {
            $($variant),*
        }

        impl $name {
            /// Returns a list of the string representations of all variants
            pub fn variants() -> Vec<String> {
                let mut variants = Vec::new();
                $(variants.push($val.to_string());)*
                variants
            }

            pub fn arguments(&self) -> &'static str {
                match self {
                    $(
                        $name::$variant => concat!($("<", $arg, "> ",)*)
                    ),*
                }
            }

            pub fn flags(&self) -> &'static str {
                match self {
                    $(
                        $name::$variant => concat!($("[", $flag, "] ",)*)
                    ),*
                }
            }

            /// Help string for a given variant.
            /// The format is "command [flag].. <arg>.. -- help message".
            pub fn cmd_help(&self) -> &'static str {
                match self {
                    $(
                        $name::$variant =>
                            concat!($val, " ", $("[", $flag, "] ",)* $("<", $arg, "> ",)* "-- ", $help)
                    ),*
                }
            }

            /// Multiline help string for `$name` including usage of all variants.
            pub fn help_msg() -> &'static str {
                concat!("Commands:\n", $(
                    "\t", $val, " ", $("[", $flag, "] ",)* $("<", $arg, "> ",)* "-- ", $help, "\n"
                ),*)
            }

        }

        impl fmt::Display for $name {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                match *self {
                    $($name::$variant => write!(f, $val)),* ,
                }
            }
        }

        impl FromStr for $name {
            type Err = ();

            fn from_str(s: &str) -> Result<$name, ()> {
                match s {
                    $($val => Ok($name::$variant)),* ,
                    _ => Err(()),
                }
            }
        }

    }
}

// `Cmd` is the declarative specification of all commands that bt-cli accepts.
gen_commands! {
    Cmd {
        Help = ("help", [], [], "Print this help message"),
        List = ("list", [], [], "List discovered services"),
        Connect = ("connect", [], ["index"], "Connect to a service"),
        ReadChr = ("read-chr", [], ["id"], "Read a characteristic"),
        ReadLongChr = ("read-long-chr", [], ["id", "offset", "max bytes"],
                        "Read a long characteristic."),
        WriteChr = ("write-chr", ["-w"], ["id", "value"],
                        "Write to a characteristic.\n\tUse the -w flag to write without response."),
        WriteLongChr = ("write-long-chr", ["-r"], ["id", "offset", "value"],
                        "Write to a long characteristic.\n\tUse the -r flag for a reliable write."),
        ReadDesc = ("read-desc", [], ["id"], "Read a characteristic descriptor"),
        ReadLongDesc = ("read-long-desc", [], ["id", "offset", "max bytes"],
                        "Read a long characteristic descriptor"),
        WriteDesc = ("write-desc", [], ["id", "value"], "Write to a characteristic descriptor"),
        WriteLongDesc = ("write-long-desc", [], ["id", "offset", "value"],
                        "Write to a long characteristic descriptor"),
        ReadByType = ("read-by-type", [], ["uuid"], "Read a characteristic or descriptor by UUID"),
        EnableNotify = ("enable-notify", [], ["id"], "Enable characteristic notifications"),
        DisableNotify = ("disable-notify", [], ["id"], "Disable characteristic notifications"),
        Quit = ("quit", [], [], "Quit and disconnect the peripheral"),
        Exit = ("exit", [], [], "Quit and disconnect the peripheral"),
    }
}

/// CmdHelper provides completion, hints, and highlighting for bt-cli
pub struct CmdHelper;

impl CmdHelper {
    pub fn new() -> CmdHelper {
        CmdHelper {}
    }
}

impl Completer for CmdHelper {
    type Candidate = String;

    fn complete(&self, line: &str, _pos: usize) -> Result<(usize, Vec<String>), ReadlineError> {
        let mut variants = Vec::new();
        for variant in Cmd::variants() {
            if variant.starts_with(line) {
                variants.push(variant)
            }
        }
        Ok((0, variants))
    }
}

impl Hinter for CmdHelper {
    /// CmdHelper provides hints for commands with arguments
    fn hint(&self, line: &str, _pos: usize) -> Option<String> {
        let needs_space = !line.ends_with(" ");
        line.trim()
            .parse::<Cmd>()
            .map(|cmd| {
                format!(
                    "{}{}{}",
                    if needs_space { " " } else { "" },
                    cmd.flags().to_string(),
                    cmd.arguments().to_string(),
                )
            })
            .ok()
    }
}

impl Highlighter for CmdHelper {
    /// CmdHelper provides highlights for commands with hints
    fn highlight_hint<'h>(&self, hint: &'h str) -> Cow<'h, str> {
        if hint.trim().is_empty() {
            Borrowed(hint)
        } else {
            Owned(format!("\x1b[90m{}\x1b[0m", hint))
        }
    }
}

/// CmdHelper can be used as an `Editor` helper for entering input commands
impl Helper for CmdHelper {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gen_commands_macro() {
        assert!(Cmd::variants().contains(&"connect".to_string()));
        assert_eq!(Cmd::WriteChr.arguments(), "<id> <value> ");
        assert_eq!(Cmd::Help.arguments(), "");
        assert_eq!(Cmd::Exit.arguments(), "");
        assert_eq!(Cmd::Quit.arguments(), "");
        assert_eq!(Cmd::WriteLongDesc.flags(), "");
        assert_eq!(Cmd::WriteDesc.flags(), "");
        assert_eq!(Cmd::WriteLongChr.flags(), "[-r] ");
        assert_eq!(Cmd::WriteChr.flags(), "[-w] ");
        assert!(Cmd::help_msg().starts_with("Commands:\n"));
    }

    #[test]
    fn test_completer() {
        let cmdhelper = CmdHelper::new();

        assert!(cmdhelper
            .complete("write-long-c", 0)
            .unwrap()
            .1
            .contains(&"write-long-chr".to_string()));
        assert!(cmdhelper.complete("he", 0).unwrap().1.contains(&"help".to_string()));
        assert!(cmdhelper.complete("ex", 0).unwrap().1.contains(&"exit".to_string()));
        assert!(cmdhelper.complete("read-d", 0).unwrap().1.contains(&"read-desc".to_string()));
        assert!(cmdhelper.complete("en", 0).unwrap().1.contains(&"enable-notify".to_string()));
    }
}
