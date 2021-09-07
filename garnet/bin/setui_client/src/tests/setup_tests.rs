// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl_fuchsia_settings::{ConfigurationInterfaces, SetupMarker, SetupRequest, SetupSettings};
use setui_client_lib::setup;

use crate::Services;
use crate::ENV_NAME;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

fn create_setup_setting(interfaces: ConfigurationInterfaces) -> SetupSettings {
    let mut settings = SetupSettings::EMPTY;
    settings.enabled_configuration_interfaces = Some(interfaces);

    settings
}

pub(crate) async fn validate_setup() -> Result<(), Error> {
    let expected_set_interfaces = ConfigurationInterfaces::Ethernet;
    let expected_watch_interfaces =
        ConfigurationInterfaces::Wifi | ConfigurationInterfaces::Ethernet;
    let env = create_service!(
        Services::Setup, SetupRequest::Set { settings, responder, } => {
            if let Some(interfaces) = settings.enabled_configuration_interfaces {
                assert_eq!(interfaces, expected_set_interfaces);
                responder.send(&mut Ok(()))?;
            } else {
                panic!("Unexpected call to set");
            }
        },
        SetupRequest::Watch { responder } => {
            responder.send(create_setup_setting(expected_watch_interfaces))?;
        }
    );

    let setup_service =
        env.connect_to_protocol::<SetupMarker>().context("Failed to connect to setup service")?;

    assert_set!(setup::command(setup_service.clone(), Some(expected_set_interfaces)));
    let output = assert_watch!(setup::command(setup_service.clone(), None));
    assert_eq!(
        output,
        setup::describe_setup_setting(&create_setup_setting(expected_watch_interfaces))
    );
    Ok(())
}
