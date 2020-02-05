// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, fx_log_info},
    settings::registry::device_storage::StashDeviceStorageFactory,
    settings::Configuration,
    settings::EnvironmentBuilder,
    settings::Runtime,
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

    // EnvironmentBuilder::spawn returns a future that can be awaited for the
    // result of the startup. Since main is a synchronous function, we cannot
    // block here and therefore continue without waiting for the result.
    let _ = EnvironmentBuilder::new(Runtime::Service(executor), Box::new(storage_factory))
        .configuration(Configuration::All)
        .spawn();

    Ok(())
}
