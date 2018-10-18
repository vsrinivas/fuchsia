// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::State,
    parking_lot::Mutex,
    rustyline::{
        completion::Completer,
        error::ReadlineError,
        Helper,
        highlight::Highlighter,
        hint::Hinter,
    },
    std::{
        borrow::Cow::{self, Borrowed, Owned},
        fmt,
        str::FromStr,
        sync::Arc,
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

            /// Return string value for a given command varient.
            pub fn as_str(&self) -> &'static str {
                match self {
                    $(
                        $name::$variant => $val
                    ),*
                }
            }

            /// Help string for a given varient. The format is "command <arg>.. -- help message"
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
            fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
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
        Connect = ("connect", ["id"], "connect to a remote device"),
        ActiveAdapter = ("adapter", [], "Show the Active Adapter"),
        GetAdapters = ("list-adapters", [], "Show all known bluetooth adapters"),
        GetDevices = ("list-devices", [], "Show all known remote devices"),
        GetDevice = ("device", ["addr"], "Show details for a known remote device"),
        StartDiscovery = ("start-discovery", [], "Start Discovery"),
        StopDiscovery = ("stop-discovery", [], "Stop Discovery"),
        Discoverable = ("discoverable", [], "Set this device to be discoverable"),
        NotDiscoverable = ("not-discoverable", [], "Revoke device discoverability"),
        Help = ("help", [], "This message"),
        Exit = ("exit", [], "Close REPL"),
        Quit = ("quit", [], "Close REPL"),
    }
}

/// CmdHelper provides completion, hints, and highlighting for bt-cli
pub struct CmdHelper {
    state: Arc<Mutex<State>>,
}

impl CmdHelper {
    pub fn new(state: Arc<Mutex<State>>) -> CmdHelper {
        CmdHelper { state }
    }
}

impl Completer for CmdHelper {
    type Candidate = String;

    // TODO(belgum): complete arguments for commands. Should be generalized to use the information
    // given by the Cmd enum with a closure for extracting a list from state.
    // Complete command variants
    fn complete(&self, line: &str, _pos: usize) -> Result<(usize, Vec<String>), ReadlineError> {
        let components: Vec<_> = line.trim_left().split_whitespace().collect();

        // Check whether we have entered a command and either whitespace or a partial argument.
        // If yes, complete arguments; if no, complete commands
        let should_complete_arguments =
            (components.len() == 1 && line.ends_with(" ")) ||
            (components.len() == 2 && !line.ends_with(" "));
        if should_complete_arguments {
            let command = components[0].trim();
            let partial_argument = components.get(1).unwrap_or(&"");
            let devices = &self.state.lock().devices;
            let mut candidates = vec![];
            if command == "connect" {
                // connect has an 'id' argument
                for id in devices.keys() {
                    if id.starts_with(partial_argument) {
                        candidates.push(format!("connect {}", id));
                    }
                }
            } else if command == "device" {
                // device has an 'addr' argument
                for device in devices.values() {
                    if device.0.address.starts_with(partial_argument) {
                        candidates.push(format!("device {}", device.0.address));
                    }
                }
            }
            Ok((0, candidates))
        } else {
            let mut variants = Vec::new();
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
    /// CmdHelper provides hints for commands with arguments
    fn hint(&self, line: &str, _pos: usize) -> Option<String> {
        let needs_space = !line.ends_with(" ");
        line.trim()
            .parse::<Cmd>()
            .map(|cmd| {
                format!("{}{}",
                    if needs_space { " " } else { "" },
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

/// Represents either continuation or breaking out of a read-evaluate-print loop.
pub enum ReplControl {
    Break,
    Continue,
}
