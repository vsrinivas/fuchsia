// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_syslog::macros::*;
use futures::channel::mpsc;
use parking_lot::{Mutex, RwLock};
use rouille::{self, router, Request, Response};
use serde;
use serde_json;
use serde_json::{to_value, Value};
use std;
use std::collections::HashMap;
use std::io::Read;
use std::sync::Arc;

// Standardized sl4f types and constants
use crate::server::constants::{COMMAND_DELIMITER, COMMAND_SIZE};
use crate::server::sl4f_types::{
    AsyncRequest, AsyncResponse, ClientData, CommandRequest, CommandResponse,
};

// Audio related includes
use crate::audio::facade::AudioFacade;

// Session related includes
use crate::basemgr::facade::BaseManagerFacade;

// Bluetooth related includes
use crate::bluetooth::ble_advertise_facade::BleAdvertiseFacade;
use crate::bluetooth::bt_control_facade::BluetoothControlFacade;
use crate::bluetooth::facade::BluetoothFacade;
use crate::bluetooth::gatt_client_facade::GattClientFacade;
use crate::bluetooth::gatt_server_facade::GattServerFacade;
use crate::bluetooth::profile_server_facade::ProfileServerFacade;

use crate::common_utils::error::Sl4fError;

// Factory related includes
use crate::factory_store::facade::FactoryStoreFacade;

// File related includes
use crate::file::facade::FileFacade;

// Logging related includes
use crate::logging::facade::LoggingFacade;

// Netstack related includes
use crate::netstack::facade::NetstackFacade;

// Paver related includes
use crate::paver::facade::PaverFacade;

// Scenic related includes
use crate::scenic::facade::ScenicFacade;

// SetUi related includes
use crate::setui::facade::SetUiFacade;

// Test related includes
use crate::test::facade::TestFacade;

// Traceutil related includes
use crate::traceutil::facade::TraceutilFacade;

// Webdriver related includes
use crate::webdriver::facade::WebdriverFacade;

// Wlan related includes
use crate::wlan::facade::WlanFacade;

/// Sl4f object. This stores all information about state for each connectivity stack.
/// Every session will have a new Sl4f object.
/// For example, to add WLAN stack support, add "wlan_facade" to the struct definition and update
/// the impl functions. Then, update method_to_fidl() to support the "wlan" method type.
#[derive(Debug, Clone)]
pub struct Sl4f {
    // audio_facade: Thread safe object for state for Audio tests
    audio_facade: Arc<AudioFacade>,

    // basemgr_facade: Thread safe object for restarting sessions for tests.
    basemgr_facade: Arc<BaseManagerFacade>,

    // bt_facade: Thread safe object for state for ble functions.
    ble_advertise_facade: Arc<BleAdvertiseFacade>,

    // bt_facade: Thread safe object for state for bluetooth connectivity tests
    bt_facade: Arc<RwLock<BluetoothFacade>>,

    // bt_control_facade: Thread safe object for state for  Bluetooth control tests
    bt_control_facade: Arc<BluetoothControlFacade>,

    // factory_store_facade: Thread safe object for state for Factory functions.
    factory_store_facade: Arc<FactoryStoreFacade>,

    // file_facade: Thread safe object for state for  File control functions
    file_facade: Arc<FileFacade>,

    // gatt_client_facade: Thread safe object for state for Gatt Client tests
    gatt_client_facade: Arc<GattClientFacade>,

    // gatt_server_facade: Thread safe object for state for Gatt Server tests
    gatt_server_facade: Arc<GattServerFacade>,

    // logging_facade: Thread safe object for state for logging functions.
    logging_facade: Arc<LoggingFacade>,

    // netstack_facade: Thread safe object for state for netstack functions.
    netstack_facade: Arc<NetstackFacade>,

    // paver_facade: Thread safe object for state for paver functions.
    paver_facade: Arc<PaverFacade>,

    // profile_server_facade: Thread safe object for state for prifle server functions.
    profile_server_facade: Arc<ProfileServerFacade>,

    // scenic_facade: thread safe object for state for Scenic functions.
    scenic_facade: Arc<ScenicFacade>,

    // setui_facade: thread safe object for state for SetUi functions.
    setui_facade: Arc<SetUiFacade>,

    // test_facade: Thread safe object for state for Test functions.
    test_facade: Arc<TestFacade>,

    // traceutil_facade: Thread safe object for state for Traceutil functions.
    traceutil_facade: Arc<TraceutilFacade>,

    // webdriver_facade: thread safe object for state for webdriver functions.
    webdriver_facade: Arc<WebdriverFacade>,

    // wlan_facade: Thread safe object for state for wlan connectivity tests
    wlan_facade: Arc<WlanFacade>,

    // clients: Thread safe map for clients that are connected to the sl4f server.
    // key = session_id (unique for every ACTS instance) and value = Data about client (see
    // sl4f_types.rs)
    clients: Arc<Mutex<HashMap<String, Vec<ClientData>>>>,
}

impl Sl4f {
    pub fn new() -> Result<Arc<RwLock<Sl4f>>, Error> {
        let audio_facade = Arc::new(AudioFacade::new()?);
        let basemgr_facade = Arc::new(BaseManagerFacade::new());
        let ble_advertise_facade = Arc::new(BleAdvertiseFacade::new());
        let bt_control_facade = Arc::new(BluetoothControlFacade::new());
        let factory_store_facade = Arc::new(FactoryStoreFacade::new());
        let file_facade = Arc::new(FileFacade::new());
        let gatt_client_facade = Arc::new(GattClientFacade::new());
        let gatt_server_facade = Arc::new(GattServerFacade::new());
        let logging_facade = Arc::new(LoggingFacade::new());
        let netstack_facade = Arc::new(NetstackFacade::new());
        let paver_facade = Arc::new(PaverFacade::new());
        let profile_server_facade = Arc::new(ProfileServerFacade::new());
        let scenic_facade = Arc::new(ScenicFacade::new());
        let setui_facade = Arc::new(SetUiFacade::new()?);
        let test_facade = Arc::new(TestFacade::new());
        let traceutil_facade = Arc::new(TraceutilFacade::new());
        let webdriver_facade = Arc::new(WebdriverFacade::new());
        let wlan_facade = Arc::new(WlanFacade::new()?);
        Ok(Arc::new(RwLock::new(Sl4f {
            audio_facade,
            basemgr_facade,
            ble_advertise_facade,
            bt_facade: BluetoothFacade::new(),
            bt_control_facade,
            factory_store_facade,
            file_facade,
            gatt_client_facade,
            gatt_server_facade,
            logging_facade,
            netstack_facade,
            paver_facade,
            profile_server_facade,
            scenic_facade,
            setui_facade,
            test_facade,
            traceutil_facade,
            webdriver_facade,
            wlan_facade,
            clients: Arc::new(Mutex::new(HashMap::new())),
        })))
    }

    pub fn get_netstack_facade(&self) -> Arc<NetstackFacade> {
        self.netstack_facade.clone()
    }

    pub fn get_audio_facade(&self) -> Arc<AudioFacade> {
        self.audio_facade.clone()
    }

    pub fn get_basemgr_facade(&self) -> Arc<BaseManagerFacade> {
        self.basemgr_facade.clone()
    }

    pub fn get_ble_advertise_facade(&self) -> Arc<BleAdvertiseFacade> {
        self.ble_advertise_facade.clone()
    }

    pub fn get_bt_control_facade(&self) -> Arc<BluetoothControlFacade> {
        self.bt_control_facade.clone()
    }

    pub fn get_factory_store_facade(&self) -> Arc<FactoryStoreFacade> {
        self.factory_store_facade.clone()
    }

    pub fn get_file_facade(&self) -> Arc<FileFacade> {
        self.file_facade.clone()
    }

    pub fn get_gatt_client_facade(&self) -> Arc<GattClientFacade> {
        self.gatt_client_facade.clone()
    }

    pub fn get_gatt_server_facade(&self) -> Arc<GattServerFacade> {
        self.gatt_server_facade.clone()
    }

    pub fn get_bt_facade(&self) -> Arc<RwLock<BluetoothFacade>> {
        self.bt_facade.clone()
    }

    pub fn get_logging_facade(&self) -> Arc<LoggingFacade> {
        self.logging_facade.clone()
    }

    pub fn get_paver_facade(&self) -> Arc<PaverFacade> {
        self.paver_facade.clone()
    }

    pub fn get_profile_server_facade(&self) -> Arc<ProfileServerFacade> {
        self.profile_server_facade.clone()
    }

    pub fn get_scenic_facade(&self) -> Arc<ScenicFacade> {
        self.scenic_facade.clone()
    }

    pub fn get_setui_facade(&self) -> Arc<SetUiFacade> {
        self.setui_facade.clone()
    }

    pub fn get_test_facade(&self) -> Arc<TestFacade> {
        self.test_facade.clone()
    }

    pub fn get_traceutil_facade(&self) -> Arc<TraceutilFacade> {
        self.traceutil_facade.clone()
    }

    pub fn get_webdriver_facade(&self) -> Arc<WebdriverFacade> {
        self.webdriver_facade.clone()
    }

    pub fn get_wlan_facade(&self) -> Arc<WlanFacade> {
        self.wlan_facade.clone()
    }

    pub fn get_clients(&self) -> Arc<Mutex<HashMap<String, Vec<ClientData>>>> {
        self.clients.clone()
    }

    pub fn cleanup_clients(&self) {
        self.clients.lock().clear();
    }

    pub fn cleanup(&mut self) {
        BluetoothFacade::cleanup(self.bt_facade.clone());
        self.ble_advertise_facade.cleanup();
        self.bt_control_facade.cleanup();
        self.gatt_client_facade.cleanup();
        self.gatt_server_facade.cleanup();
        self.cleanup_clients();
    }

    pub fn print_clients(&self) {
        fx_log_info!("SL4F Clients: {:?}", self.clients);
    }

    // Add *_facade.print() when new Facade objects are added (i.e WlanFacade)
    pub fn print(&self) {
        self.bt_facade.read().print();
        self.ble_advertise_facade.print();
        self.bt_control_facade.print();
        self.gatt_client_facade.print();
        self.gatt_server_facade.print();
    }
}

// Handles all incoming requests to SL4F server, routes accordingly
pub fn serve(
    request: &Request,
    sl4f_session: Arc<RwLock<Sl4f>>,
    rouille_sender: mpsc::UnboundedSender<AsyncRequest>,
) -> Response {
    router!(request,
        (GET) (/) => {
            // Parse the command request
            fx_log_info!(tag: "serve", "Received command request.");
            client_request(sl4f_session.clone(), &request, rouille_sender.clone())
        },
        (GET) (/init) => {
            // Initialize a client
            fx_log_info!(tag: "serve", "Received init request.");
            client_init(&request, sl4f_session.write().get_clients().clone())
        },
        (GET) (/print_clients) => {
            // Print information about all clients
            fx_log_info!(tag: "serve", "Received print client request.");
            const PRINT_ACK: &str = "Successfully printed clients.";
            sl4f_session.read().print_clients();
            rouille::Response::json(&PRINT_ACK)
        },
        (GET) (/cleanup) => {
            fx_log_info!(tag: "serve", "Received server cleanup request.");
            server_cleanup(&request, sl4f_session.clone())
        },
        _ => {
            fx_log_err!(tag: "serve", "Received unknown server request.");
            const FAIL_REQUEST_ACK: &str = "Unknown GET request.";
            let res = CommandResponse::new("".to_string(), None, serde::export::Some(FAIL_REQUEST_ACK.to_string()));
            rouille::Response::json(&res)
        }
    )
}

// Given the session id, method id, and result of FIDL call, store the result for this client
fn store_response(
    sl4f_session: Arc<RwLock<Sl4f>>,
    client_id: String,
    method_id: String,
    result: AsyncResponse,
) {
    let clients = sl4f_session.write().clients.clone();

    // If the current client session is found, append the result of the FIDL call to the result
    // history
    if clients.lock().contains_key(&client_id) {
        let command_response = ClientData::new(method_id.clone(), result.clone());
        clients.lock().entry(client_id.clone()).or_insert(Vec::new()).push(command_response);
    } else {
        fx_log_err!(tag: "store_response", "Client doesn't exist in server database: {:?}", client_id);
    }

    fx_log_info!(tag: "store_response", "Stored response. Updated clients: {:?}", clients);
}

// Given the request, map the test request to a FIDL query and execute
// asynchronously
fn client_request(
    sl4f_session: Arc<RwLock<Sl4f>>,
    request: &Request,
    rouille_sender: mpsc::UnboundedSender<AsyncRequest>,
) -> Response {
    const FAIL_TEST_ACK: &str = "Command failed";

    let (session_id, method_id, method_type, method_name, method_params) =
        match parse_request(request) {
            Ok(res) => res,
            Err(e) => {
                fx_log_err!(tag: "client_request", "Failed to parse request. {:?}", e);
                return Response::json(&FAIL_TEST_ACK);
            }
        };

    // Create channel for async thread to respond to
    // Package response and ship over JSON RPC
    let (async_sender, rouille_receiver) = std::sync::mpsc::channel();
    let req =
        AsyncRequest::new(async_sender, method_id.clone(), method_type, method_name, method_params);
    rouille_sender.unbounded_send(req).expect("Failed to send request to async thread.");
    let resp: AsyncResponse = rouille_receiver.recv().unwrap();

    store_response(sl4f_session, session_id.clone(), method_id.clone(), resp.clone());
    fx_log_info!(tag: "client_request", "Received async thread response: {:?}", resp);

    // If the response has a return value, package into response, otherwise use error code
    match resp.result {
        Some(async_res) => {
            let res = CommandResponse::new(method_id, Some(async_res), None);
            rouille::Response::json(&res)
        }
        None => {
            let res = CommandResponse::new(method_id, None, resp.error);
            rouille::Response::json(&res)
        }
    }
}

// Initializes a new client, adds to clients, a thread-safe HashMap
// Returns a rouille::Response
fn client_init(
    request: &Request,
    clients: Arc<Mutex<HashMap<String, Vec<ClientData>>>>,
) -> Response {
    const INIT_ACK: &str = "Recieved init request.";
    const FAIL_INIT_ACK: &str = "Failed to init client.";

    let (_, _, _, _, method_params) = match parse_request(request) {
        Ok(res) => res,
        Err(_) => return Response::json(&FAIL_INIT_ACK),
    };

    let client_id_raw = match method_params.get("client_id") {
        Some(id) => Some(id).unwrap().clone(),
        None => return Response::json(&FAIL_INIT_ACK),
    };

    // Initialize client with key = id, val = client data
    let client_id = client_id_raw.as_str().map(String::from).unwrap();
    let client_data = Vec::new();

    if clients.lock().contains_key(&client_id) {
        fx_log_warn!(tag: "client_init",
            "Key: {:?} already exists in clients. ",
            &client_id
        );
        return rouille::Response::json(&FAIL_INIT_ACK);
    }

    clients.lock().insert(client_id, client_data);
    fx_log_info!(tag: "client_init", "Updated clients: {:?}", clients);

    rouille::Response::json(&INIT_ACK)
}

// Given the name of the ACTS method, derive method type + method name
// Returns two "", "" on invalid input which will later propagate to
// method_to_fidl and raise an error
fn split_string(method_name_raw: String) -> (String, String) {
    let split = method_name_raw.split(COMMAND_DELIMITER);
    let string_split: Vec<&str> = split.collect();

    // Input must be two strings separated by "."
    if string_split.len() != COMMAND_SIZE {
        return ("".to_string(), "".to_string());
    };

    (string_split[0].to_string(), string_split[1].to_string())
}

// Given a request, grabs the method id, name, and parameters
// Return Sl4fError if fail
fn parse_request(request: &Request) -> Result<(String, String, String, String, Value), Error> {
    let mut data = match request.data() {
        Some(d) => d,
        None => return Err(Sl4fError::new("Failed to parse request buffer.").into()),
    };

    let mut buf: String = String::new();
    if data.read_to_string(&mut buf).is_err() {
        return Err(Sl4fError::new("Failed to read request buffer.").into());
    }

    // Ignore the json_rpc field
    let request_data: CommandRequest = match serde_json::from_str(&buf) {
        Ok(tdata) => tdata,
        Err(_) => return Err(Sl4fError::new("Failed to unpack request data.").into()),
    };

    let method_id_raw = request_data.id.clone();
    let method_name_raw = request_data.method.clone();
    let method_params = request_data.params.clone();
    fx_log_info!(tag: "parse_request",
        "method id: {:?}, name: {:?}, args: {:?}",
        method_id_raw, method_name_raw, method_params
    );

    // Separate the method_name field of the request into the method type (e.g bluetooth) and the
    // actual method name itself
    let (method_type, method_name) = split_string(method_name_raw.clone());
    let (session_id, method_id) = split_string(method_id_raw.clone());
    Ok((session_id, method_id, method_type, method_name, method_params))
}

fn server_cleanup(request: &Request, sl4f_session: Arc<RwLock<Sl4f>>) -> Response {
    const FAIL_CLEANUP_ACK: &str = "Failed to cleanup SL4F resources.";
    const CLEANUP_ACK: &str = "Successful cleanup of SL4F resources.";

    fx_log_info!(tag: "server_cleanup", "Cleaning up server state");
    let (_, method_id, _, _, _) = match parse_request(request) {
        Ok(res) => res,
        Err(_) => return Response::json(&FAIL_CLEANUP_ACK),
    };

    // Cleanup all resources associated with session
    // Validate result
    let session = sl4f_session.clone();
    session.write().cleanup();
    session.read().print();

    let ack = match to_value(serde::export::Some(CLEANUP_ACK.to_string())) {
        Ok(v) => CommandResponse::new(method_id, Some(v), None),
        Err(e) => CommandResponse::new(method_id, None, Some(e.to_string())),
    };

    rouille::Response::json(&ack)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn split_string_test() {
        // Standard command
        let mut method_name = "bt.send".to_string();
        assert_eq!(("bt".to_string(), "send".to_string()), split_string(method_name));

        // Invalid command (should result in empty result)
        method_name = "bluetooth_send".to_string();
        assert_eq!(("".to_string(), "".to_string()), split_string(method_name));

        // Too many separators in command
        method_name = "wlan.scan.start".to_string();
        assert_eq!(("".to_string(), "".to_string()), split_string(method_name));

        // Empty command
        method_name = "".to_string();
        assert_eq!(("".to_string(), "".to_string()), split_string(method_name));

        // No separator
        method_name = "BluetoothSend".to_string();
        assert_eq!(("".to_string(), "".to_string()), split_string(method_name));

        // Invalid separator
        method_name = "Bluetooth,Scan".to_string();
        assert_eq!(("".to_string(), "".to_string()), split_string(method_name));
    }
}
