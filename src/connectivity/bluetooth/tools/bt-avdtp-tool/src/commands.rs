// Copyright 2019 The Fuchsia Authors. All rights reserved.
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

            /// Multiline help string for `$name` including usage of all variants
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

// `Cmd` is the declarative specification of all commands that bt-avdtp-tool accepts.
// TODO(fxbug.dev/37089): Add support for printing with arguments (merge PrintPeer, PrintPeers).
gen_commands! {
    Cmd {
        AbortStream = ("abort-stream", ["generic id"], "Initiate an abort stream command"),
        EstablishStream = ("establish-stream", ["generic id"], "Establish streaming on the device"),
        ReleaseStream = ("release-stream", ["generic id"], "Release the stream"),
        StartStream = ("start-stream", ["generic id"], "Initiate a start stream command"),
        SetConfig = ("set-configuration", ["generic id"], "Set configuration"),
        GetConfig = ("get-configuration", ["generic id"], "Get configuration"),
        GetCapabilities = ("get-capabilities", ["generic id"], "Get capabilities"),
        GetAllCapabilities = ("get-all-capabilities", ["generic id"], "Get all capabilities"),
        Reconfigure = ("reconfigure", ["generic id"], "Reconfigure command"),
        Suspend = ("suspend", ["generic id"], "Suspend command"),
        SuspendReconfigure = ("suspend-reconfigure", ["generic id"],
            "Suspend command followed by reconfigure."),
        Help = ("help", [], "Print command help"),
        Exit = ("exit", [], "Quit the program"),
        Quit = ("quit", [], "Quit the program"),
    }
}

/// CmdHelper provides completion, hints, and highlighting for bt-avdtp-tool
pub struct CmdHelper {}

impl CmdHelper {
    pub fn new() -> CmdHelper {
        CmdHelper {}
    }
}

impl Completer for CmdHelper {
    type Candidate = String;

    fn complete(&self, line: &str, _pos: usize) -> Result<(usize, Vec<String>), ReadlineError> {
        let components: Vec<_> = line.trim_start().split_whitespace().collect();

        // Check whether we have entered a command and either whitespace or a partial argument.
        // If yes, complete arguments; if no, complete commands
        let mut variants = Vec::new();
        let should_complete_arguments = (components.len() == 1 && line.ends_with(" "))
            || (components.len() == 2 && !line.ends_with(" "));
        if should_complete_arguments {
            let candidates = vec![];
            Ok((0, candidates))
        } else {
            for variant in Cmd::variants() {
                if variant.starts_with(line) {
                    variants.push(variant)
                }
            }
            Ok((0, variants))
        }
    }
}

impl Hinter for CmdHelper {
    /// Provide a hint for what argument should be presented next.
    /// Returns None if no hint is available.
    fn hint(&self, line: &str, _pos: usize) -> Option<String> {
        let needs_space = !line.ends_with(" ");
        line.trim()
            .parse::<Cmd>()
            .map(|cmd| {
                format!("{}{}", if needs_space { " " } else { "" }, cmd.arguments().to_string(),)
            })
            .ok()
    }
}

impl Highlighter for CmdHelper {
    /// Highlighter changes the text color of the hint, to differentiate between user input
    /// and the hint itself.
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_gen_commands_macro() {
        assert!(Cmd::variants().contains(&"establish-stream".to_string()));
        assert_eq!(Cmd::ReleaseStream.arguments(), "<generic id> ");
        assert_eq!(Cmd::Help.arguments(), "");
        assert_eq!(Cmd::Exit.arguments(), "");
        assert_eq!(Cmd::Quit.arguments(), "");
        assert!(Cmd::help_msg().starts_with("Commands:\n"));
    }

    #[test]
    fn test_completer() {
        let cmdhelper = CmdHelper::new();
        assert!(cmdhelper
            .complete("establ", 0)
            .unwrap()
            .1
            .contains(&"establish-stream".to_string()));
        assert!(cmdhelper.complete("he", 0).unwrap().1.contains(&"help".to_string()));
        assert!(cmdhelper.complete("sus", 0).unwrap().1.contains(&"suspend".to_string()));
        assert!(cmdhelper
            .complete("suspend-", 0)
            .unwrap()
            .1
            .contains(&"suspend-reconfigure".to_string()));
        assert!(cmdhelper
            .complete("get-a", 0)
            .unwrap()
            .1
            .contains(&"get-all-capabilities".to_string()));
    }
}
