// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use log::warn;
use omaha_client::{
    common::{App, UserCounting, Version},
    configuration::{Config, Updater},
    protocol::{request::OS, Cohort},
};
use std::fs;
use std::io;

pub fn get_apps() -> Result<Vec<App>, Error> {
    let id = fs::read_to_string("/config/build-info/omaha_product_id")?;
    // We use the console build id as the version.
    let version: Version = fs::read_to_string("/config/build-info/omaha_build_id")?.parse()?;
    // Fuchsia only has a single app.
    Ok(vec![App {
        id,
        version,
        fingerprint: None,
        cohort: Cohort::default(),
        user_counting: UserCounting::ClientRegulatedByDate(None),
    }])
}

pub fn get_config() -> Config {
    // This OS version is for metrics purpose only, so it's ok if we can't read it.
    let path = "/config/build-info/version";
    let version = fs::read_to_string(path).unwrap_or_else(|err| {
        if err.kind() != io::ErrorKind::NotFound {
            warn!("error reading {}: {}", path, err);
        }
        "".to_string()
    });
    // trim_end() removes extra new line at the end of the file.
    let version = version.trim_end().to_string();

    Config {
        updater: Updater { name: "Fuchsia".to_string(), version: Version::from([0, 0, 1, 0]) },

        os: OS {
            platform: "Fuchsia".to_string(),
            version,
            service_pack: "".to_string(),
            arch: std::env::consts::ARCH.to_string(),
        },

        service_url: "https://clients2.google.com/service/update2/fuchsia/json".to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_get_config() {
        let config = get_config();
        assert_eq!(config.updater.name, "Fuchsia");
        let os = config.os;
        assert_eq!(os.platform, "Fuchsia");
        assert_eq!(os.arch, std::env::consts::ARCH);
        assert_eq!(config.service_url, "https://clients2.google.com/service/update2/fuchsia/json");
    }
}
