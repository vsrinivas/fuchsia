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
    CheckNow(CheckNow),
    MonitorUpdates(MonitorUpdates),

    // fuchsia.update.installer protocol:
    ForceInstall(ForceInstall),

    // fuchsia.update CommitStatusProvider protocol:
    WaitForCommit(WaitForCommit),
    Revert(Revert),
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
#[argh(subcommand, name = "monitor-updates")]
/// Monitor all update checks.
pub struct MonitorUpdates {}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "force-install")]
/// Directly invoke the system updater to install the provided update, bypassing any update checks.
pub struct ForceInstall {
    /// whether or not the system updater should reboot into the new system.
    #[argh(option, default = "true")]
    pub reboot: bool,

    /// the url of the update package describing the update to install. Ex:
    /// fuchsia-pkg://fuchsia.example/update.
    #[argh(positional)]
    pub update_pkg_url: String,

    /// the force install was initiated by a service, in the background.
    #[argh(switch)]
    pub service_initiated: bool,
}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "wait-for-commit")]
/// Wait for the update to be committed.
pub struct WaitForCommit {}

#[derive(Debug, Eq, FromArgs, PartialEq)]
#[argh(subcommand, name = "revert")]
/// Revert the update.
pub struct Revert {}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

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
                    service_initiated: false,
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
                    service_initiated: false,
                })
            }
        );
    }

    #[test]
    fn test_force_install_service_initiated() {
        let update =
            Update::from_args(&["update"], &["force-install", "--service-initiated", "url"])
                .unwrap();
        assert_eq!(
            update,
            Update {
                cmd: Command::ForceInstall(ForceInstall {
                    update_pkg_url: "url".to_owned(),
                    reboot: true,
                    service_initiated: true,
                })
            }
        );
    }

    #[test]
    fn test_wait_for_commit() {
        let update = Update::from_args(&["update"], &["wait-for-commit"]).unwrap();
        assert_eq!(update, Update { cmd: Command::WaitForCommit(WaitForCommit {}) });
    }

    #[test]
    fn test_revert() {
        let update = Update::from_args(&["update"], &["revert"]).unwrap();
        assert_eq!(update, Update { cmd: Command::Revert(Revert {}) });
    }

    #[test]
    fn test_monitor() {
        let update = Update::from_args(&["update"], &["monitor-updates"]).unwrap();
        assert_eq!(update, Update { cmd: Command::MonitorUpdates(MonitorUpdates {}) });
    }
}
