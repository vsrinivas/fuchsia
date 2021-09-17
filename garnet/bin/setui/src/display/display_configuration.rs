// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file contains a number of enums and structs that are used as an
//! internal representation of the configuration data found in
//! `/config/data/display_configuration.json`.

use serde::{Deserialize, Serialize};

/// Possible theme modes that can be found in
/// `/config/data/display_configuration.json`.
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum ConfigurationThemeMode {
    Auto,
}

/// Possible theme types that can be found in
/// `/config/data/display_configuration.json`.
#[derive(PartialEq, Debug, Clone, Copy, Serialize, Deserialize)]
pub enum ConfigurationThemeType {
    Light,
}

/// Internal representation of the display configuration stored in
/// `/config/data/display_configuration.json`.
#[derive(PartialEq, Debug, Clone, Deserialize)]
pub struct DisplayConfiguration {
    pub theme: ThemeConfiguration,
}

/// Internal representation of the theme portion of the configuration stored in
/// `/config/data/display_configuration.json`.
#[derive(PartialEq, Debug, Clone, Deserialize)]
pub struct ThemeConfiguration {
    pub theme_mode: Vec<ConfigurationThemeMode>,
    pub theme_type: ConfigurationThemeType,
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::config::default_settings::DefaultSetting;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_display_configuration() {
        let default_value = DefaultSetting::<DisplayConfiguration, &str>::new(
            None,
            "/config/data/display_configuration.json",
        )
        .load_default_value()
        .expect("Invalid display configuration")
        .expect("Unable to parse configuration");

        assert_eq!(default_value.theme.theme_mode, vec![ConfigurationThemeMode::Auto]);
        assert_eq!(default_value.theme.theme_type, ConfigurationThemeType::Light);
    }
}
