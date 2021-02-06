// Copyright 2020 The Fuchsia Authors. All rights reserved.
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

//TODO(fxbug.dev/46991): Make the macro a library
/// Macro to generate a command enum and its impl.
macro_rules! gen_commands {
    ($name:ident {
        $($variant:ident = ($val:expr, [$($arg:expr),*], $help:expr)),*,
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

            /// Help string for a given variant. The format is "command <arg>.. -- help message"
            #[allow(unused)]
            pub fn cmd_help(&self) -> &'static str {
                match self {
                    $(
                        $name::$variant => concat!($val, " ", $("<", $arg, "> ",)* "-- ", $help)
                    ),*
                }
            }

            /// Multiline help string for `$name` including usage of all variants.
            pub fn help_msg() -> &'static str {
                concat!("Commands:\n", $(
                    "\t", $val, " ", $("<", $arg, "> ",)* "-- ", $help, "\n"
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

gen_commands! {
    Command {
        Get = ("get", [],
            "Get all information of the battery state"),
        Set = ("set", ["<attribute1 value1> ... [attributeN valueN]", "--help"],
            "Set state of battery in one line | Print help message"),
        Reconnect = ("reconnect", [],
            "Connect the real battery and disconnect the simulator"),
        Disconnect = ("disconnect", [],
            "Disconnect the real battery and connnect to the simulator"),
        Help = ("help", [], "Help message"),
        Exit = ("exit", [], "Exit/Close REPL"),
        Quit = ("quit", [], "Quit/Close REPL"),

    }
}

/// CmdHelper provides completion, hints, and highlighting for battery-simulator
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
        for variant in Command::variants() {
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
            .parse::<Command>()
            .map(|cmd| {
                format!("{}{}", if needs_space { " " } else { "" }, cmd.arguments().to_string(),)
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

/// Represents either continuation or breaking out of a read-evaluate-print loop.
pub enum ReplControl {
    Break,
    Continue,
}
