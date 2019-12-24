// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::StreamExt,
    parking_lot::RwLock,
    settings::create_fidl_service,
    settings::registry::device_storage::StashDeviceStorageFactory,
    settings::service_context::ServiceContext,
    settings::switchboard::base::get_all_setting_types,
    std::sync::Arc,
};

const STASH_IDENTITY: &str = "settings_service";

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let mut executor = fasync::Executor::new()?;

    let service_context = Arc::new(RwLock::new(ServiceContext::new(None)));

    let mut fs = ServiceFs::new();

    let storage_factory = StashDeviceStorageFactory::create(
        STASH_IDENTITY,
        connect_to_service::<fidl_fuchsia_stash::StoreMarker>().unwrap(),
    );

    create_fidl_service(
        fs.dir("svc"),
        get_all_setting_types(),
        service_context,
        Box::new(storage_factory),
    );

    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}
