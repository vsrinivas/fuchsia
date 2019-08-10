// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {failure::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: IntlProxy,
    time_zone: Option<fidl_fuchsia_intl::TimeZoneId>,
    temperature_unit: Option<fidl_fuchsia_intl::TemperatureUnit>,
    locales: Vec<fidl_fuchsia_intl::LocaleId>,
) -> Result<String, Error> {
    let mut output = String::new();

    if let Some(time_zone_value) = time_zone {
        let time_zone_string = time_zone_value.id.clone();

        let mut settings = IntlSettings::empty();
        settings.time_zone_id = Some(time_zone_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                output.push_str(&format!("Successfully set time_zone to {}", time_zone_string))
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if let Some(temperature_unit_value) = temperature_unit {
        let mut settings = IntlSettings::empty();
        settings.temperature_unit = Some(temperature_unit_value);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => output.push_str(&format!(
                "Successfully set temperature unit to {}",
                describe_temperature_unit(&temperature_unit_value)
            )),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else if !locales.is_empty() {
        let locales_description = describe_locales(&locales);

        let mut settings = IntlSettings::empty();
        settings.locales = Some(locales);

        let mutate_result = proxy.set(settings).await?;
        match mutate_result {
            Ok(_) => {
                output.push_str(&format!("Successfully set locales to {}", locales_description))
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        let setting = proxy.watch().await?;

        match setting {
            Ok(setting_value) => {
                let setting_string = describe_intl_setting(&setting_value);
                output.push_str(&setting_string);
            }
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    }

    Ok(output)
}

fn describe_intl_setting(intl_setting: &IntlSettings) -> String {
    let mut output = String::new();

    output.push_str("Intl { ");

    if let Some(time_zone) = &intl_setting.time_zone_id {
        output.push_str(&format!("time_zone: {} ", time_zone.id));
    }

    if let Some(temperature_unit) = &intl_setting.temperature_unit {
        output.push_str(&format!(
            "temperature_unit: {} ",
            describe_temperature_unit(temperature_unit)
        ));
    }

    if let Some(locales) = &intl_setting.locales {
        output.push_str(&describe_locales(locales));
    }

    output.push_str("}");

    output
}

fn describe_temperature_unit(temperature_unit: &fidl_fuchsia_intl::TemperatureUnit) -> String {
    if temperature_unit == &fidl_fuchsia_intl::TemperatureUnit::Celsius {
        "Celsius".to_string()
    } else {
        "Fahrenheit".to_string()
    }
}

fn describe_locales(locales: &Vec<fidl_fuchsia_intl::LocaleId>) -> String {
    let mut output = String::new();
    output.push_str("locales: [ ");

    for locale in locales.iter() {
        output.push_str(&format!("{}, ", locale.id));
    }

    output.push_str("]");
    output
}
