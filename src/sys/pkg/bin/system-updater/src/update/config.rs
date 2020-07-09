// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::metrics,
    crate::args,
    fuchsia_url::pkg_url::PkgUrl,
    std::time::{Instant, SystemTime},
};

/// Configuration for an update attempt.
#[derive(Debug, PartialEq, Eq)]
pub struct Config {
    pub(super) initiator: args::Initiator,
    pub(super) source_version: String,
    pub(super) target_version: String,
    pub(super) update_url: PkgUrl,
    pub(super) should_reboot: bool,
    pub(super) should_write_recovery: bool,
    pub(super) start_time: SystemTime,
    pub(super) start_time_mono: Instant,
}

impl Config {
    /// Constructs update configuration from command line arguments, filling in details as needed.
    pub fn from_args(args: args::Args) -> Self {
        // The OTA attempt started during the update check, so use that time if possible. Fallback
        // to now if that data wasn't provided.
        let start_time = args.start.unwrap_or_else(SystemTime::now);
        let start_time_mono =
            metrics::system_time_to_monotonic_time(start_time).unwrap_or_else(Instant::now);

        Self {
            initiator: args.initiator,
            source_version: args.source,
            target_version: args.target,
            update_url: args.update,
            should_reboot: args.reboot,
            should_write_recovery: !args.skip_recovery,
            start_time,
            start_time_mono,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::time::Duration};

    #[test]
    fn config_from_args() {
        let now = SystemTime::now() - Duration::from_secs(3600);

        let args = args::Args {
            initiator: args::Initiator::Manual,
            source: "source-version".to_string(),
            target: "target-version".to_string(),
            update: "fuchsia-pkg://example.com/foo".parse().unwrap(),
            reboot: true,
            skip_recovery: false,
            start: Some(now),
            oneshot: true,
        };

        let mut config = Config::from_args(args);

        let d = config.start_time_mono.elapsed();
        assert!(
            d > Duration::from_secs(3550) && d < Duration::from_secs(3650),
            "expected start_time_mono to be an hour before now, got delta of: {:?}",
            d
        );

        let now_mono = Instant::now();

        config.start_time_mono = now_mono;
        assert_eq!(
            config,
            Config {
                initiator: args::Initiator::Manual,
                source_version: "source-version".to_string(),
                target_version: "target-version".to_string(),
                update_url: "fuchsia-pkg://example.com/foo".parse().unwrap(),
                should_reboot: true,
                should_write_recovery: true,
                start_time: now,
                start_time_mono: now_mono,
            }
        );
    }
}
