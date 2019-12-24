// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_settings::*};

pub async fn command(
    proxy: SetupProxy,
    configuration_interfaces: Option<ConfigurationInterfaces>,
) -> Result<String, Error> {
    let mut output = String::new();

    if let Some(configuration_interfaces) = configuration_interfaces {
        let mut settings = SetupSettings::empty();
        settings.enabled_configuration_interfaces = Some(configuration_interfaces);

        let set_result = proxy.set(settings).await?;

        match set_result {
            Ok(_) => output.push_str(&format!("Successfully set configuration interfaces")),
            Err(err) => output.push_str(&format!("{:?}", err)),
        }
    } else {
        output.push_str(&describe_setup_setting(&proxy.watch().await?));
    }

    Ok(output)
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
