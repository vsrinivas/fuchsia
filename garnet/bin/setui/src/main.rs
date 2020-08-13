// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::lock::Mutex,
    settings::agent::earcons,
    settings::agent::restore_agent,
    settings::config::default_settings::DefaultSetting,
    settings::handler::device_storage::StashDeviceStorageFactory,
    settings::switchboard::base::get_default_setting_types,
    settings::EnabledServicesConfiguration,
    settings::EnvironmentBuilder,
    settings::ServiceConfiguration,
    settings::ServiceFlags,
    std::sync::Arc,
};

const STASH_IDENTITY: &str = "settings_service";

fn main() -> Result<(), Error> {
    let executor = fasync::Executor::new()?;

    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let storage_factory = StashDeviceStorageFactory::create(
        STASH_IDENTITY,
        connect_to_service::<fidl_fuchsia_stash::StoreMarker>().unwrap(),
    );

    let default_configuration =
        EnabledServicesConfiguration::with_services(get_default_setting_types());

    let configuration =
        DefaultSetting::new(default_configuration, Some("/config/data/service_configuration.json"))
            .get_default_value();

    let flags =
        DefaultSetting::new(ServiceFlags::default(), Some("/config/data/service_flags.json"))
            .get_default_value();

    let configuration = ServiceConfiguration::from(configuration, flags);

    // EnvironmentBuilder::spawn returns a future that can be awaited for the
    // result of the startup. Since main is a synchronous function, we cannot
    // block here and therefore continue without waiting for the result.
    EnvironmentBuilder::new(Arc::new(Mutex::new(storage_factory)))
        .configuration(configuration)
        .agents(&[restore_agent::blueprint::create(), earcons::agent::blueprint::create()])
        .spawn(executor)
}
