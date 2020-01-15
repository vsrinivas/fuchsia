// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_avrcp::AvcPanelCommand,
    rustyline::{
        completion::Completer, error::ReadlineError, highlight::Highlighter, hint::Hinter, Helper,
    },
    std::{
        borrow::Cow::{self, Borrowed, Owned},
        fmt,
        str::FromStr,
    },
};

macro_rules! gen_avc_commands {
($name:ident {
    $($variant:ident = ($long:expr, $short:expr)),*,
}) => {

        static AVC_COMMAND_LONG_VARIANTS: &[&str] = &[$($long,)*];
        static AVC_COMMAND_SHORT_VARIANTS: &[&str] = &[$($short,)*];

        pub fn avc_match_string(s: &str) -> Option<AvcPanelCommand> {
            let lc = s.to_lowercase();
            match lc.as_str() {
                $($short => return Some(AvcPanelCommand::$variant)),*,
                _ => {}
            }
            match lc.as_str() {
                $($long => Some(AvcPanelCommand::$variant)),*,
                _ => None
            }
        }
    }
}

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
            pub fn cmd_help(&self) -> &'static str {
                match self {
                    $(
                        $name::$variant => concat!($val, " ", $("<", $arg, "> ",)* "-- ", $help)
                    ),*
                }
            }

            /// Multiline help string for `$name` including usage of all variants.
            pub fn help_msg() -> String {
                let command_help = concat!($(
                    "\t", $val, " ", $("<", $arg, "> ",)* "-- ", $help, "\n"
                ),*);

                format!("Commands:\n{}\nAVC keys:\n\tkey {}\n", command_help, AVC_COMMAND_LONG_VARIANTS.join("\n\tkey "))
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

gen_avc_commands! {
    AvcCommand {
        Select = ("select", "sel"),
        Up = ("up", "up"),
        Down = ("down", "do"),
        Left = ("left", "le"),
        Right = ("right", "ri"),
        RootMenu = ("rootmenu", "rm"),
        ContentsMenu = ("contentsmenu", "cm"),
        FavoriteMenu = ("favoritemenu", "fm"),
        Exit = ("exit", "ex"),
        OnDemandMenu = ("ondemandmenu", "om"),
        AppsMenu = ("appsmenu", "am"),
        Key0 = ("key0", "0"),
        Key1 = ("key1", "1"),
        Key2 = ("key2", "2"),
        Key3 = ("key3", "3"),
        Key4 = ("key4", "4"),
        Key5 = ("key5", "5"),
        Key6 = ("key6", "6"),
        Key7 = ("key7", "7"),
        Key8 = ("key8", "8"),
        Key9 = ("key9", "9"),
        Dot = ("dot", "."),
        Enter = ("enter", "en"),
        ChannelUp = ("channelup", "cu"),
        ChannelDown = ("channeldown", "cd"),
        ChannelPrevious = ("channelprevious", "cp"),
        InputSelect = ("inputselect", "ip"),
        Info = ("info", "in"),
        Help = ("help", "he"),
        PageUp = ("pageup", "pu"),
        PageDown = ("pagedown", "pd"),
        Lock = ("lock", "lo"),
        Power = ("power", "po"),
        VolumeUp = ("volumeup", "vu"),
        VolumeDown = ("volumedown", "vd"),
        Mute = ("mute", "m"),
        Play = ("play", "pl"),
        Stop = ("stop", "s"),
        Pause = ("pause", "pa"),
        Record = ("record", "rec"),
        Rewind = ("rewind", "rew"),
        FastForward = ("fastforward", "ff"),
        Eject = ("eject", "ej"),
        Forward = ("forward", "fw"),
        Backward = ("backward", "bw"),
        List = ("list", "l"),
        F1 = ("f1","f1"),
        F2 = ("f2","f2"),
        F3 = ("f3","f3"),
        F4 = ("f4","f4"),
        F5 = ("f5","f5"),
        F6 = ("f6","f6"),
        F7 = ("f7","f7"),
        F8 = ("f8","f8"),
        F9 = ("f9","f9"),
        Red = ("red", "re"),
        Green = ("green", "gr"),
        Blue = ("blue", "bl"),
        Yellow = ("yellow", "ye"),
    }
}

// `Cmd` is the declarative specification of all commands that bt-cli accepts.
gen_commands! {
    Cmd {
        AvcCommand = ("key", ["command"], "send an AVC passthrough keypress event"),
        GetMediaAttributes = ("get-media", [], "gets currently playing media attributes"),
        GetPlayStatus = ("get-play-status", [], "gets the status of the currently playing media at the TG"),
        GetPlayerApplicationSettings = ("get-player-application-settings",
            ["Optional: id1 id2 ..."],
            "Gets currently set attribute values if ids are specified. Otherwise, all possible attribute values on the TG."),
        SetPlayerApplicationSettings = ("set-player-application-settings", [],
            "Sets player application settings with a default Equalizer=Off setting."),
        SupportedEvents = ("get-supported-events", [], "gets the supported events of the target"),
        SendRawVendorCommand = ("send-raw-vendor-command", ["pdu_id", "payload"], "send a raw vendor AVC command"),
        SetVolume =  ("set-volume", ["volume"], "send a set absolute volume command (0-127)"),
        IsConnected = ("connection-status", [], "checks if the current device is current connected"),
        Help = ("help", [], "This message"),
        Exit = ("exit", [], "Close REPL"),
        Quit = ("quit", [], "Close REPL"),
    }
}

/// CmdHelper provides completion, hints, and highlighting for bt-cli
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
        // Check whether we have entered a command and either whitespace or a partial argument.
        // If yes, complete arguments; if no, complete commands
        let should_complete_arguments = (components.len() == 1 && line.ends_with(" "))
            || (components.len() == 2 && !line.ends_with(" "));
        if should_complete_arguments {
            let command = components[0].trim();
            let partial_argument = components.get(1).unwrap_or(&"");
            let mut candidates = vec![];
            if command == "key" {
                // connect and device have 'id|addr' arguments
                // can match against peer identifier or address
                for key in AVC_COMMAND_LONG_VARIANTS {
                    if key.starts_with(partial_argument) {
                        candidates.push(format!("{} {}", command, key));
                    }
                }
                for key in AVC_COMMAND_SHORT_VARIANTS {
                    if key.starts_with(partial_argument) {
                        candidates.push(format!("{} {}", command, key));
                    }
                }
            }
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
    /// CmdHelper provides hints for commands with arguments
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_avc_match_string() {
        assert_eq!(Some(AvcPanelCommand::Up), avc_match_string("up"));
        assert_eq!(Some(AvcPanelCommand::Up), avc_match_string("UP"));
        assert_eq!(None, avc_match_string("UPS"));
        assert_eq!(Some(AvcPanelCommand::Play), avc_match_string("play"));
        assert_eq!(Some(AvcPanelCommand::Play), avc_match_string("pl"));
        assert_eq!(None, avc_match_string("p"));
        assert_eq!(None, avc_match_string(""));
        assert_eq!(None, avc_match_string("12"));
        assert_eq!(Some(AvcPanelCommand::Dot), avc_match_string("."));
        assert_eq!(Some(AvcPanelCommand::ChannelUp), avc_match_string("channelup"));
    }

    #[test]
    fn test_completer() {
        let cmdhelper = CmdHelper::new();
        assert!(cmdhelper.complete("ke", 0).unwrap().1.contains(&"key".to_string()));
        assert!(cmdhelper.complete("get", 0).unwrap().1.contains(&"get-media".to_string()));
        assert!(cmdhelper.complete("key ex", 0).unwrap().1.contains(&"key exit".to_string()));
        assert!(cmdhelper
            .complete("conne", 0)
            .unwrap()
            .1
            .contains(&"connection-status".to_string()));
        assert!(cmdhelper
            .complete("send-ra", 0)
            .unwrap()
            .1
            .contains(&"send-raw-vendor-command".to_string()));
        assert!(cmdhelper
            .complete("get-s", 0)
            .unwrap()
            .1
            .contains(&"get-supported-events".to_string()));
        assert!(cmdhelper
            .complete("get-play-", 0)
            .unwrap()
            .1
            .contains(&"get-play-status".to_string()));
        assert!(cmdhelper
            .complete("get-playe", 0)
            .unwrap()
            .1
            .contains(&"get-player-application-settings".to_string()));
        assert!(cmdhelper
            .complete("set", 0)
            .unwrap()
            .1
            .contains(&"set-player-application-settings".to_string()));
        assert!(cmdhelper.complete("set-v", 0).unwrap().1.contains(&"set-volume".to_string()));
    }
}
