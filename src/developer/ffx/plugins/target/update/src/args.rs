// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Manage updates: query/set update channel, kick off a check for update, force
/// an update (to any point, i.e. a downgrade can be requested).
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "update", description = "Update base system software on target device")]
pub struct Update {
    #[argh(subcommand)]
    pub cmd: Command,
}

/// Subcommands for `update`.
#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum Command {
    // fuchsia.update.channelcontrol.ChannelControl protocol:
    Channel(Channel),

    // fuchsia.update Manager protocol:
    CheckNow(CheckNow),

    // fuchsia.update.installer protocol:
    ForceInstall(ForceInstall),
}

/// Get the current (running) channel.
#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "channel")]
pub struct Channel {
    #[argh(subcommand)]
    pub cmd: channel::Command,
}

/// Subcommands for `channel`.
// TODO(fxb/60016): Make get/set symmetrical.
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

    /// Get the current (running) channel.
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand, name = "get", description = "Current channel, may change after update.")]
    pub struct Get {}

    /// Get the target channel.
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(
        subcommand,
        name = "target",
        description = "Differs from `get` when the next update changes update channel."
    )]
    pub struct Target {}

    /// Set the target channel.
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand, name = "set", description = "Set a `target` channel.")]
    pub struct Set {
        #[argh(positional)]
        pub channel: String,
    }

    /// List the known target channels.
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(subcommand, name = "list")]
    pub struct List {}
}

/// Start an update. If no newer update is available, no update is performed.
#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "check-now", description = "Do update, if available.")]
pub struct CheckNow {
    /// the update check was initiated by a service, in the background.
    #[argh(switch)]
    pub service_initiated: bool,

    /// monitor for state update.
    #[argh(switch)]
    pub monitor: bool,
}

/// Directly invoke the system updater to install the provided update, bypassing
/// any update checks.
#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "force-install")]
pub struct ForceInstall {
    /// whether or not the system updater should reboot into the new system.
    #[argh(option, default = "true")]
    pub reboot: bool,

    /// the url of the update package describing the update to install. Ex:
    /// fuchsia-pkg://fuchsia.com/update.
    #[argh(positional)]
    pub update_pkg_url: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn test_unknown_option() {
        assert_matches!(Update::from_args(&["update"], &["--unknown"]), Err(_));
    }

    #[test]
    fn test_unknown_subcommand() {
        assert_matches!(Update::from_args(&["update"], &["unknown"]), Err(_));
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
            Update { cmd: Command::CheckNow(CheckNow { service_initiated: false, monitor: true }) }
        );
    }

    #[test]
    fn test_check_now_service_initiated() {
        let update = Update::from_args(&["update"], &["check-now", "--service-initiated"]).unwrap();
        assert_eq!(
            update,
            Update { cmd: Command::CheckNow(CheckNow { service_initiated: true, monitor: false }) }
        );
    }

    #[test]
    fn test_force_install_requires_positional_arg() {
        assert_matches!(Update::from_args(&["update"], &["force-install"]), Err(_));
    }

    #[test]
    fn test_force_install() {
        let update = Update::from_args(&["update"], &["force-install", "url"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::ForceInstall(ForceInstall {
                    update_pkg_url: "url".to_owned(),
                    reboot: true,
                })
            }
        );
    }

    #[test]
    fn test_force_install_no_reboot() {
        let update =
            Update::from_args(&["update"], &["force-install", "--reboot", "false", "url"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::ForceInstall(ForceInstall {
                    update_pkg_url: "url".to_owned(),
                    reboot: false,
                })
            }
        );
    }
}
