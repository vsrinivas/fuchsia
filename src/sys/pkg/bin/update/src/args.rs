// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(Debug, Eq, FromArgs, PartialEq)]
/// Manage updates.
pub struct Update {
    #[argh(subcommand)]
    pub cmd: Command,
}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum Command {
    // fuchsia.update.channelcontrol.ChannelControl protocol:
    Channel(Channel),

    // fuchsia.update Manager protocol:
    State(State),
    CheckNow(CheckNow),
    Monitor(Monitor),
}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "channel")]
/// Get the current (running) channel.
pub struct Channel {
    #[argh(subcommand)]
    pub cmd: channel::Command,
}

pub mod channel {
    use argh::FromArgs;

    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand)]
    pub enum Command {
        Get(Get),
        Target(Target),
        Set(Set),
        List(List),
    }

    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand, name = "get")]
    /// Get the current (running) channel.
    pub struct Get {}

    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand, name = "target")]
    /// Get the target channel.
    pub struct Target {}

    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand, name = "set")]
    /// Set the target channel.
    pub struct Set {
        #[argh(positional)]
        pub channel: String,
    }

    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand, name = "list")]
    /// List of known target channels.
    pub struct List {}
}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "state")]
/// Print the current update state.
pub struct State {}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "check-now")]
/// Start an update.
pub struct CheckNow {
    /// the update check was initiated by a service, in the background.
    #[argh(switch)]
    pub service_initiated: bool,

    /// monitor for state update.
    #[argh(switch)]
    pub monitor: bool,
}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "monitor")]
/// Monitor an in-progress update.
pub struct Monitor {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_unknown_option() {
        assert!(Update::from_args(&["update"], &["--unkown"]).is_err());
    }

    #[test]
    fn test_unknown_subcommand() {
        assert!(Update::from_args(&["update"], &["unkown"]).is_err());
    }

    #[test]
    fn test_channel_get() {
        let update = Update::from_args(&["update"], &["channel", "get"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::Channel(Channel { cmd: channel::Command::Get(channel::Get {}) })
            }
        );
    }

    #[test]
    fn test_channel_target() {
        let update = Update::from_args(&["update"], &["channel", "target"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::Channel(Channel {
                    cmd: channel::Command::Target(channel::Target {})
                })
            }
        );
    }

    #[test]
    fn test_channel_set() {
        let update = Update::from_args(&["update"], &["channel", "set", "new-channel"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::Channel(Channel {
                    cmd: channel::Command::Set(channel::Set { channel: "new-channel".to_string() })
                })
            }
        );
    }

    #[test]
    fn test_channel_list() {
        let update = Update::from_args(&["update"], &["channel", "list"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::Channel(Channel { cmd: channel::Command::List(channel::List {}) })
            }
        );
    }

    #[test]
    fn test_state() {
        let update = Update::from_args(&["update"], &["state"]).unwrap();
        assert_eq!(
            update,
            Update { cmd: Command::State(State {}) }
        );
    }

    #[test]
    fn test_check_now() {
        let update = Update::from_args(&["update"], &["check-now"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::CheckNow(CheckNow { service_initiated: false, monitor: false })
            }
        );
    }
    #[test]
    fn test_check_now_monitor() {
        let update = Update::from_args(&["update"], &["check-now", "--monitor"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::CheckNow(CheckNow { service_initiated: false, monitor: true })
            }
        );
    }
    #[test]
    fn test_check_now_service_initiated() {
        let update = Update::from_args(&["update"], &["check-now", "--service-initiated"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::CheckNow(CheckNow { service_initiated: true, monitor: false })
            }
        );
    }
    #[test]
    fn test_monitor() {
        let update = Update::from_args(&["update"], &["monitor"]).unwrap();
        assert_eq!(
            update,
            Update { cmd: Command::Monitor(Monitor {}) }
        );
    }
}
