// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

/// Manage updates: query/set update channel, kick off a check for update, force
/// an update (to any point, i.e. a downgrade can be requested).
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "update",
    description = "Update base system software on target",
    note = "This command interfaces with system update services on the target."
)]
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
#[argh(
    subcommand,
    name = "channel",
    description = "View and manage update channels",
    note = "Channel management commands and operations. Interfaces directly with
the 'fuchsia.update.channelcontrol.ChannelControl' service on the target
system."
)]
pub struct Channel {
    #[argh(subcommand)]
    pub cmd: channel::Command,
}

/// Subcommands for `channel`.
// TODO(fxbug.dev/60016): Make get/set symmetrical.
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

    /// Get the current channel
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(
        subcommand,
        name = "get-current",
        description = "Return the currently configured update channel",
        note = "For developer product configurations, this is by default 'devhost'.",
        error_code(1, "Timeout while getting update channel.")
    )]
    pub struct Get {}

    /// Get the target channel
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(
        subcommand,
        name = "get-next",
        description = "Return the next or target update channel",
        note = "Returns the next or target channel. This differs from `get` when the
next successful update changes the configured update channel on the
system.",
        error_code(1, "Timeout while getting update channel.")
    )]
    pub struct Target {}

    /// Set the target channel
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(
        subcommand,
        name = "set",
        example = "To list all the known update channels:

    $ ffx target update channel list

Then, use a valid channel from the list:

    $ ffx target update channel set <channel>",
        description = "Sets the update channel",
        note = "Sets the next or target update channel on the device. When paired with
`ffx target update check-now`, ensures the update is check against the
next or target channel. When the update is successful, next or target
channel becomes the current channel.

Use `ffx target update channel list` to list known system update
channels.",
        error_code(1, "Timeout while setting update channel.")
    )]
    pub struct Set {
        #[argh(positional)]
        pub channel: String,
    }

    /// List the known target channels
    #[derive(Debug, Eq, FromArgs, PartialEq)]
    #[argh(
        subcommand,
        name = "list",
        description = "List the known update channels",
        note = "This lists all the known next or target update channels on the system.

Returns an empty list if no other update channels are configured.",
        error_code(1, "Timeout while getting list of update channel.")
    )]
    pub struct List {}
}

/// Start an update. If no newer update is available, no update is performed.
#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(
    subcommand,
    name = "check-now",
    example = "To check for update and monitor progress:

    $ ffx target update check-now --monitor",
    description = "Check and perform the system update operation",
    note = "Triggers an update check operation and performs the update if available.
Interfaces using the 'fuchsia.update Manager' protocol with the system
update service on the target.

The command takes in an optional `--monitor` switch to watch the progress
of the update. The output is displayed in `stdout`.

The command also takes an optional `--service-initiated` switch to indicate
a separate service has initiated a check for update."
)]
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
#[argh(
    subcommand,
    name = "force-install",
    example = "With a known update package URL, trigger an update:

    $ ffx target update force-install fuchsia-pkg://fuchsia.com/update

Also trigger a reboot after update:

    $ ffx target update force-install
    fuchsia-pkg://fuchsia.com/update
    --reboot",
    description = "Trigger the system updater manually",
    note = "Directly invoke the system updater to install the provided update,
bypassing any update checks.

Interfaces using the 'fuchsia.update.installer' protocol to update the
system. Requires an <update_pkg_url> in the following format:

`fuchsia-pkg://fuchsia.com/update`

Takes an optional `--reboot <true|false>` to trigger a system reboot
after update has been successfully applied."
)]
pub struct ForceInstall {
    /// automatically trigger a reboot into the new system
    #[argh(option, default = "true")]
    pub reboot: bool,

    /// the url of the update package describing the update to install
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
        let update = Update::from_args(&["update"], &["channel", "get-current"]).unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::Channel(Channel { cmd: channel::Command::Get(channel::Get {}) })
            }
        );
    }

    #[test]
    fn test_channel_target() {
        let update = Update::from_args(&["update"], &["channel", "get-next"]).unwrap();
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
