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

            /// Multiline help string for `$name` including usage of all variants.
            pub fn help_msg() -> &'static str {
                concat!("Commands:\n", $(
                    "\t", $val, " ", $("<", $arg, "> ",)* "-- ", $help, "\n"
                ),*, "\nSee //src/connectivity/bluetooth/tools/bt-bredr-profile/README.md for documentation.\n")
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
    Cmd {
        Advertise = ("advertise", ["psm", "channel-mode", "max-rx-du-size"],
                      "\n\t\tRegister a service with the SDP server.\n\
                       \t\t'psm' is a protocol id that this service will advertise support for.\n\
                       \t\t'channel-mode' is {basic|ertm}.\n\
                       \t\t'max-rx-sdu-size' is an integer in the range 0 - 65535.\n\n\
                       \t\tExample: advertise 25 basic 672"),
        Services = ("services", [], "List registered services"),
        RemoveService = ("remove-service", ["service-id"],
                         "\n\t\tUnregister service corresponding to 'service-id'\n\
                          \t\tExample: remove-service 0"),
        Channels = ("channels", [], "List connected channels and their Ids assigned by the REPL"),
        Connect = ("connect", ["peer-id", "psm", "channel-mode", "max-rx-sdu-size", "security-requirements"],
                        "\n\t\tCreate an l2cap channel to the remote device 'peer-id'. \n\
                         \t\t'channel-mode' must be {basic|ertm}. 'psm' and 'max-rx-sdu-size' must be\n\
                         \t\tpositive integers in the range 0 - 65535. 'security-requirements' must be\n\
                         \t\t{none|auth|sc|auth-sc}.\n\n\
                         \t\tExample: connect-l2cap 028565803f1368b2 1 basic 672 none"),
        Disconnect = ("disconnect", ["channel-id"],
                           "\n\t\tDrop socket corresponding to 'channel-id', which will disconnect\n\
                            \t\tthe l2cap channel.\n\
                            \t\t'channel-id' must correspond to a connected channel listed by the \n\
                            \t\t'channels' command\n\n\
                            \t\tExample: disconnect-l2cap 0"),
        Write = ("write", ["channel-id", "data"],
                "\n\t\tWrite 'data' on the socket/channel represented by 'channel-id'\n\n\
                 \t\tExample: write 0 0123456789abcd"),
        Help = ("help", [], "Print command help"),
        Exit = ("exit", [], "Remove all services, close all channels, and exit the REPL."),
        Quit = ("quit", [], "Alias for 'exit'."),
    }
}

/// Represents either continuation or breaking out of a read-evaluate-print loop.
pub enum ReplControl {
    Break,
    Continue,
}

/// CmdHelper provides completion, hints, and highlighting.
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
