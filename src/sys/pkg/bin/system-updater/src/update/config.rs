// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::metrics,
    fidl_fuchsia_update_installer_ext::{Initiator as ExtInitiator, Options},
    fuchsia_url::pkg_url::PkgUrl,
    std::time::{Instant, SystemTime},
};

/// Configuration for an update attempt.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct Config {
    pub(super) initiator: Initiator,
    pub update_url: PkgUrl,
    pub should_write_recovery: bool,
    pub(super) start_time: SystemTime,
    pub(super) start_time_mono: Instant,
    pub allow_attach_to_existing_attempt: bool,
}

impl Config {
    /// Constructs update configuration from PkgUrl and Options.
    pub fn from_url_and_options(update_url: PkgUrl, options: Options) -> Self {
        let start_time = SystemTime::now();
        let start_time_mono =
            metrics::system_time_to_monotonic_time(start_time).unwrap_or_else(Instant::now);

        Self {
            initiator: options.initiator.into(),
            update_url,
            should_write_recovery: options.should_write_recovery,
            start_time,
            start_time_mono,
            allow_attach_to_existing_attempt: options.allow_attach_to_existing_attempt,
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum Initiator {
    Automatic,
    Manual,
}

impl From<Initiator> for ExtInitiator {
    fn from(args_initiator: Initiator) -> Self {
        match args_initiator {
            Initiator::Manual => ExtInitiator::User,
            Initiator::Automatic => ExtInitiator::Service,
        }
    }
}

impl From<ExtInitiator> for Initiator {
    fn from(ext_initiator: ExtInitiator) -> Self {
        match ext_initiator {
            ExtInitiator::User => Initiator::Manual,
            ExtInitiator::Service => Initiator::Automatic,
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
                initiator: ExtInitiator::User,
            },
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn config_from_url_and_options() {
        let options = Options {
            initiator: ExtInitiator::User,
            allow_attach_to_existing_attempt: true,
            should_write_recovery: true,
        };
        let update_url = "fuchsia-pkg://example.com/foo".parse().unwrap();

        let config = Config::from_url_and_options(update_url, options);

        matches::assert_matches!(
            config,
            Config {
                initiator: Initiator::Manual,
                update_url: url,
                should_write_recovery: true,
                allow_attach_to_existing_attempt: true,
                ..
            } if url == "fuchsia-pkg://example.com/foo".parse().unwrap()
        );
    }
}
