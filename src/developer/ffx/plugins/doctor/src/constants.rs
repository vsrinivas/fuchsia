// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const BUG_URL: &str = "https://bugs.fuchsia.dev/p/fuchsia/issues/entry?template=ffx+User+Bug";

pub const SUCCESS: &str = "success";
pub const FOUND: &str = "found";
pub const FAILED_TIMEOUT: &str = "FAILED. Timed out.";
pub const FAILED_WITH_ERROR: &str = "FAILED. Error was:";

pub const DAEMON_CHECK_INTRO: &str = "First, bringing up a working daemon...";
pub const DAEMON_RUNNING_CHECK: &str = "Checking for a running daemon...";
pub const NONE_RUNNING: &str = "none running.";
pub const KILLING_ZOMBIE_DAEMONS: &str = "Attempting to kill any zombie daemons...";
pub const ZOMBIE_KILLED: &str = "killed at least one daemon.";
pub const SPAWNING_DAEMON: &str = "Starting a new daemon instance...";
pub const CONNECTING_TO_DAEMON: &str =
    "Attempting to connect to the daemon. This may take a couple seconds...";
pub const COMMUNICATING_WITH_DAEMON: &str = "Attempting to communicate with the daemon...";
pub const DAEMON_CHECKS_FAILED: &str = "✗ Failed to spawn and connect to a daemon. Please file a bug to the ffx team using the link below and include all above output.";
pub const LISTING_TARGETS_NO_FILTER: &str = "Attempting to list all targets...";
pub const NO_TARGETS_FOUND_SHORT: &str = "No targets found. ";
pub const NO_TARGETS_FOUND_EXTENDED: &str = "No targets found. Make sure your devices are connected and running and try again in a few seconds.
If this persists after verifying your device's connection, please file a bug at the link below and include 1) all output above and 2) device syslog if available.";
pub const TARGET_CHOICE_HELP: &str =
    "To choose a different target, use `fx ffx --target \"<nodename>\" doctor`";
pub const TARGET_SUMMARY: &str = "Target summary:";
pub const CONNECTING_TO_RCS: &str = "Attempting to get an RCS connection...";
pub const COMMUNICATING_WITH_RCS: &str = "Attempting to communicate with RCS...";
pub const RCS_TERMINAL_FAILURE: &str =
    "To resolve repeated failures to connect to RCS, try rebooting failing devices.";
pub const RCS_TERMINAL_FAILURE_BUG_INSTRUCTIONS: &str = "If this persists, please file a bug at the link below and include 1) all output above and 2) device syslog if available.";
