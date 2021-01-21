// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{IntlProxy, IntlSettings};

pub async fn command(
    proxy: IntlProxy,
    time_zone: Option<fidl_fuchsia_intl::TimeZoneId>,
    temperature_unit: Option<fidl_fuchsia_intl::TemperatureUnit>,
    locales: Vec<fidl_fuchsia_intl::LocaleId>,
    hour_cycle: Option<fidl_fuchsia_settings::HourCycle>,
    clear_locales: bool,
) -> WatchOrSetResult {
    let mut settings = IntlSettings::EMPTY;
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
    settings.hour_cycle = hour_cycle;

    Ok(if settings == IntlSettings::EMPTY {
        // No values set, perform a watch instead.
        Either::Watch(utils::watch_to_stream(proxy, |p| p.watch()))
    } else {
        Either::Set(match proxy.set(settings).await? {
            Ok(_) => format!("Successfully set IntlSettings"),
            Err(err) => format!("{:#?}", err),
        })
    })
}
