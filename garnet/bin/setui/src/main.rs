// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![feature(async_await, await_macro, vec_remove_item)]
// TODO(brycelee): Remove once switchboard is fully integrated. Here to allow for
// test code.
#![allow(dead_code)]
use {
    crate::default_store::DefaultStore,
    crate::json_codec::JsonCodec,
    crate::mutation::*,
    crate::registry::registry_impl::RegistryImpl,
    crate::setting_adapter::{MutationHandler, SettingAdapter},
    crate::switchboard::base::SettingAction,
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    failure::Error,
    fidl_fuchsia_settings::*,
    fidl_fuchsia_setui::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::StreamExt,
    log::error,
    setui_handler::SetUIHandler,
    std::sync::Arc,
    system_handler::SystemStreamHandler,
};

mod common;
mod default_store;
mod fidl_clone;
mod json_codec;
mod mutation;
mod registry;
mod setting_adapter;
mod setui_handler;
mod switchboard;
mod system_handler;

fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["setui-service"]).expect("Can't init logger");
    fx_log_info!("Starting setui-service...");

    let mut executor = fasync::Executor::new()?;

    let (action_tx, action_rx) = futures::channel::mpsc::unbounded::<SettingAction>();

    // Creates switchboard, handed to interface implementations to send messages
    // to handlers.
    let (_switchboard_handle, event_tx) = SwitchboardImpl::create(action_tx);

    // Creates registry, used to register handlers for setting types.
    let _registry_handle = RegistryImpl::create(event_tx, action_rx);

    let mut fs = ServiceFs::new();
    let handler = Arc::new(SetUIHandler::new());
    let system_handler = Arc::new(SystemStreamHandler::new(handler.clone()));

    // TODO(SU-210): Remove once other adapters are ready.
    handler.register_adapter(Box::new(SettingAdapter::new(
        SettingType::Unknown,
        Box::new(DefaultStore::new("/data/unknown.dat".to_string(), Box::new(JsonCodec::new()))),
        MutationHandler { process: &process_string_mutation, check_sync: None },
        None,
    )));

    handler.register_adapter(Box::new(SettingAdapter::new(
        SettingType::Account,
        Box::new(DefaultStore::new("/data/account.dat".to_string(), Box::new(JsonCodec::new()))),
        MutationHandler {
            process: &process_account_mutation,
            check_sync: Some(&should_sync_account_mutation),
        },
        Some(SettingData::Account(AccountSettings { mode: None })),
    )));

    let handler_clone = handler.clone();
    fs.dir("svc").add_fidl_service(move |stream: SetUiServiceRequestStream| {
        let handler_clone = handler_clone.clone();

        fx_log_info!("Connecting to setui_service");
        fasync::spawn(async move {
            await!(handler_clone.handle_stream(stream))
                .unwrap_or_else(|e| error!("Failed to spawn {:?}", e))
        });
    });

    // Register for the new settings APIs as well.
    fs.dir("svc").add_fidl_service(move |stream: SystemRequestStream| {
        let system_handler_clone = system_handler.clone();
        fx_log_info!("Connecting to System");
        fasync::spawn(async move {
            await!(system_handler_clone.handle_system_stream(stream))
                .unwrap_or_else(|e| error!("Failed to spawn {:?}", e))
        });
    });

    fs.take_and_serve_directory_handle()?;
    let () = executor.run_singlethreaded(fs.collect());
    Ok(())
}
