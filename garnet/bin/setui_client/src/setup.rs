// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::utils::{self, Either, WatchOrSetResult};
use fidl_fuchsia_settings::{ConfigurationInterfaces, SetupProxy, SetupSettings};

pub async fn command(
    proxy: SetupProxy,
    configuration_interfaces: Option<ConfigurationInterfaces>,
) -> WatchOrSetResult {
    Ok(if let Some(configuration_interfaces) = configuration_interfaces {
        let mut settings = SetupSettings::EMPTY;
        settings.enabled_configuration_interfaces = Some(configuration_interfaces);

        let set_result = proxy.set(settings).await?;

        Either::Set(match set_result {
            Ok(_) => format!("Successfully set configuration interfaces"),
            Err(err) => format!("{:?}", err),
        })
    } else {
        Either::Watch(utils::formatted_watch_to_stream(
            proxy,
            |p| p.watch(),
            |s| describe_setup_setting(&s),
        ))
    })
}

pub fn describe_setup_setting(setup_settings: &SetupSettings) -> String {
    let mut output = String::new();

    if let Some(config_interfaces) = &setup_settings.enabled_configuration_interfaces {
        output.push_str(&describe_interfaces(*config_interfaces));
    } else {
        output.push_str("no configuration interfaces set");
    }

    return output;
}

fn describe_interfaces(interfaces: ConfigurationInterfaces) -> String {
    let mut interface_labels = Vec::new();

    if interfaces.intersects(ConfigurationInterfaces::Ethernet) {
        interface_labels.push("ethernet");
    }

    if interfaces.intersects(ConfigurationInterfaces::Wifi) {
        interface_labels.push("WiFi");
    }

    return interface_labels.join("|");
}
