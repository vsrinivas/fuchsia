// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_settings::{IntlProxy, IntlSettings};

pub async fn command(
    proxy: IntlProxy,
    time_zone: Option<fidl_fuchsia_intl::TimeZoneId>,
    temperature_unit: Option<fidl_fuchsia_intl::TemperatureUnit>,
    locales: Vec<fidl_fuchsia_intl::LocaleId>,
    clear_locales: bool,
) -> Result<String, Error> {
    let mut settings = IntlSettings::empty();
    if clear_locales {
        settings.locales = Some(vec![]);
    } else if locales.len() > 0 {
        settings.locales = Some(locales);
    } else {
        // No locales specified and clear_locales not set, don't change the locales value.
        settings.locales = None;
    }
    settings.temperature_unit = temperature_unit;
    settings.time_zone_id = time_zone;

    if settings == IntlSettings::empty() {
        // No values set, perform a watch instead.
        match proxy.watch().await? {
            Ok(setting_value) => Ok(format!("{:#?}", setting_value)),
            Err(err) => Ok(format!("{:#?}", err)),
        }
    } else {
        match proxy.set(settings).await? {
            Ok(_) => Ok(format!("Successfully set IntlSettings")),
            Err(err) => Ok(format!("{:#?}", err)),
        }
    }
}
