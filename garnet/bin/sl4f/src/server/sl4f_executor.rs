// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_syslog::macros::*;
use futures::channel::mpsc;
use futures::StreamExt;
use parking_lot::RwLock;
use serde_json::Value;
use std::sync::Arc;

// Sl4f related inclusions
use crate::server::sl4f::Sl4f;
use crate::server::sl4f_types::{AsyncRequest, AsyncResponse, FacadeType};

// Translation layers go here (i.e netstack_method_to_fidl)
use crate::audio::commands::audio_method_to_fidl;
use crate::basemgr::commands::base_manager_method_to_fidl;
use crate::bluetooth::commands::ble_advertise_method_to_fidl;
use crate::bluetooth::commands::ble_method_to_fidl;
use crate::bluetooth::commands::bt_control_method_to_fidl;
use crate::bluetooth::commands::gatt_client_method_to_fidl;
use crate::bluetooth::commands::gatt_server_method_to_fidl;
use crate::bluetooth::commands::profile_server_method_to_fidl;
use crate::common_utils::error::Sl4fError;
use crate::factory_store::commands::factory_store_method_to_fidl;
use crate::file::commands::file_method_to_fidl;
use crate::logging::commands::logging_method_to_fidl;
use crate::netstack::commands::netstack_method_to_fidl;
use crate::paver::commands::paver_method_to_fidl;
use crate::scenic::commands::scenic_method_to_fidl;
use crate::setui::commands::setui_method_to_fidl;
use crate::test::commands::test_method_to_fidl;
use crate::traceutil::commands::traceutil_method_to_fidl;
use crate::webdriver::commands::webdriver_method_to_fidl;
use crate::wlan::commands::wlan_method_to_fidl;

pub async fn run_fidl_loop(
    sl4f_session: Arc<RwLock<Sl4f>>,
    receiver: mpsc::UnboundedReceiver<AsyncRequest>,
) {
    const CONCURRENT_REQ_LIMIT: usize = 10; // TODO(CONN-6) figure out a good parallel value for this

    let session = &sl4f_session;
    let handler = |request| {
        async move {
            handle_request(Arc::clone(session), request).await.unwrap();
        }
    };

    let receiver_fut = receiver.for_each_concurrent(CONCURRENT_REQ_LIMIT, handler);

    receiver_fut.await;
}

async fn handle_request(
    sl4f_session: Arc<RwLock<Sl4f>>,
    request: AsyncRequest,
) -> Result<(), Error> {
    match request {
        AsyncRequest { tx, id, method_type, name, params } => {
            let curr_sl4f_session = sl4f_session.clone();
            fx_log_info!(tag: "run_fidl_loop",
                         "Received synchronous request: {:?}, {:?}, {:?}, {:?}, {:?}",
                         tx, id, method_type, name, params);
            match method_to_fidl(method_type, name, params, curr_sl4f_session).await {
                Ok(response) => {
                    let async_response = AsyncResponse::new(Ok(response));

                    // Ignore any tx sending errors since there is not a recovery path.  The
                    // connection to the test server may be broken.
                    let _ = tx.send(async_response);
                }
                Err(e) => {
                    fx_log_err!("Error returned from calling method_to_fidl: {}", e);
                    let async_response = AsyncResponse::new(Err(e));

                    // Ignore any tx sending errors since there is not a recovery path.  The
                    // connection to the test server may be broken.
                    let _ = tx.send(async_response);
                }
            };
        }
    }
    Ok(())
}

async fn method_to_fidl(
    method_type: String,
    method_name: String,
    args: Value,
    sl4f_session: Arc<RwLock<Sl4f>>,
) -> Result<Value, Error> {
    match FacadeType::from_str(&method_type) {
        FacadeType::AudioFacade => {
            audio_method_to_fidl(method_name, args, sl4f_session.read().get_audio_facade()).await
        }
        FacadeType::BaseManagerFacade => {
            base_manager_method_to_fidl(method_name, args, sl4f_session.read().get_basemgr_facade())
                .await
        }
        FacadeType::BleAdvertiseFacade => {
            ble_advertise_method_to_fidl(
                method_name,
                args,
                sl4f_session.read().get_ble_advertise_facade(),
            )
            .await
        }
        FacadeType::Bluetooth => {
            ble_method_to_fidl(method_name, args, sl4f_session.read().get_bt_facade()).await
        }
        FacadeType::BluetoothControlFacade => {
            bt_control_method_to_fidl(
                method_name,
                args,
                sl4f_session.read().get_bt_control_facade(),
            )
            .await
        }
        FacadeType::FactoryStoreFacade => {
            factory_store_method_to_fidl(
                method_name,
                args,
                sl4f_session.read().get_factory_store_facade(),
            )
            .await
        }
        FacadeType::FileFacade => {
            file_method_to_fidl(method_name, args, sl4f_session.read().get_file_facade()).await
        }
        FacadeType::GattClientFacade => {
            gatt_client_method_to_fidl(
                method_name,
                args,
                sl4f_session.read().get_gatt_client_facade(),
            )
            .await
        }
        FacadeType::GattServerFacade => {
            gatt_server_method_to_fidl(
                method_name,
                args,
                sl4f_session.read().get_gatt_server_facade(),
            )
            .await
        }
        FacadeType::LoggingFacade => {
            logging_method_to_fidl(method_name, args, sl4f_session.read().get_logging_facade())
                .await
        }
        FacadeType::NetstackFacade => {
            netstack_method_to_fidl(method_name, args, sl4f_session.read().get_netstack_facade())
                .await
        }
        FacadeType::ProfileServerFacade => {
            profile_server_method_to_fidl(
                method_name,
                args,
                sl4f_session.read().get_profile_server_facade(),
            )
            .await
        }
        FacadeType::ScenicFacade => {
            scenic_method_to_fidl(method_name, args, sl4f_session.read().get_scenic_facade()).await
        }
        FacadeType::SetUiFacade => {
            setui_method_to_fidl(method_name, args, sl4f_session.read().get_setui_facade()).await
        }
        FacadeType::TestFacade => {
            test_method_to_fidl(method_name, args, sl4f_session.read().get_test_facade()).await
        }
        FacadeType::TraceutilFacade => {
            traceutil_method_to_fidl(method_name, args, sl4f_session.read().get_traceutil_facade())
                .await
        }
        FacadeType::WebdriverFacade => {
            webdriver_method_to_fidl(method_name, args, sl4f_session.read().get_webdriver_facade())
                .await
        }
        FacadeType::Wlan => {
            wlan_method_to_fidl(method_name, args, sl4f_session.read().get_wlan_facade()).await
        }
        FacadeType::Paver => {
            paver_method_to_fidl(method_name, args, sl4f_session.read().get_paver_facade()).await
        }
        _ => {
            Err(Sl4fError::new(&format!("Invalid FIDL method type {:?}", &method_type).to_string())
                .into())
        }
    }
}
