// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::metrics,
    crate::args,
    fidl_fuchsia_update_installer_ext::Options,
    fuchsia_url::pkg_url::PkgUrl,
    std::time::{Instant, SystemTime},
};

/// Configuration for an update attempt.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct Config {
    pub(super) initiator: args::Initiator,
    pub(super) source_version: String,
    pub(super) target_version: String,
    pub update_url: PkgUrl,
    pub(super) should_reboot: bool,
    pub should_write_recovery: bool,
    pub(super) start_time: SystemTime,
    pub(super) start_time_mono: Instant,
    pub allow_attach_to_existing_attempt: bool,
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
            allow_attach_to_existing_attempt: false,
        }
    }

    /// Constructs update configuration from PkgUrl and Options.
    pub fn from_url_and_options(update_url: PkgUrl, options: Options) -> Self {
        let start_time = SystemTime::now();
        let start_time_mono =
            metrics::system_time_to_monotonic_time(start_time).unwrap_or_else(Instant::now);

        Self {
            initiator: options.initiator.into(),
            source_version: "".to_string(),
            target_version: "".to_string(),
            update_url,
            should_reboot: true,
            should_write_recovery: options.should_write_recovery,
            start_time,
            start_time_mono,
            allow_attach_to_existing_attempt: options.allow_attach_to_existing_attempt,
        }
    }
}

#[cfg(test)]
pub struct ConfigBuilder<'a> {
    update_url: &'a str,
    should_write_recovery: bool,
    allow_attach_to_existing_attempt: bool,
}

#[cfg(test)]
impl<'a> ConfigBuilder<'a> {
    pub fn new() -> Self {
        Self {
            update_url: "fuchsia-pkg://fuchsia.com/update",
            allow_attach_to_existing_attempt: false,
            should_write_recovery: true,
        }
    }
    pub fn update_url(mut self, update_url: &'a str) -> Self {
        self.update_url = update_url;
        self
    }
    pub fn allow_attach_to_existing_attempt(
        mut self,
        allow_attach_to_existing_attempt: bool,
    ) -> Self {
        self.allow_attach_to_existing_attempt = allow_attach_to_existing_attempt;
        self
    }
    pub fn should_write_recovery(mut self, should_write_recovery: bool) -> Self {
        self.should_write_recovery = should_write_recovery;
        self
    }
    pub fn build(self) -> Result<Config, anyhow::Error> {
        Ok(Config::from_url_and_options(
            self.update_url.parse()?,
            Options {
                allow_attach_to_existing_attempt: self.allow_attach_to_existing_attempt,
                should_write_recovery: self.should_write_recovery,
                initiator: fidl_fuchsia_update_installer_ext::Initiator::User,
            },
        ))
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
                allow_attach_to_existing_attempt: false,
            }
        );
    }

    #[test]
    fn config_from_url_and_options() {
        let options = Options {
            initiator: fidl_fuchsia_update_installer_ext::Initiator::User,
            allow_attach_to_existing_attempt: true,
            should_write_recovery: true,
        };
        let update_url = "fuchsia-pkg://example.com/foo".parse().unwrap();

        let config = Config::from_url_and_options(update_url, options);

        matches::assert_matches!(
            config,
            Config {
                initiator: args::Initiator::Manual,
                update_url: url,
                should_write_recovery: true,
                allow_attach_to_existing_attempt: true,
                ..
            } if url == "fuchsia-pkg://example.com/foo".parse().unwrap()
        );
    }
}
