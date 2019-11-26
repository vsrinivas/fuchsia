// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_syslog::macros::*;
use futures::channel::mpsc;
use maplit::{convert_args, hashmap};
use parking_lot::RwLock;
use rouille::{self, router, Request, Response};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::io::Read;
use std::sync::Arc;

// Standardized sl4f types and constants
use crate::server::constants::{COMMAND_DELIMITER, COMMAND_SIZE};
use crate::server::sl4f_types::{
    AsyncCommandRequest, AsyncRequest, AsyncResponse, ClientData, CommandRequest, CommandResponse,
    Facade,
};

// Audio related includes
use crate::audio::facade::AudioFacade;

// Session related includes
use crate::basemgr::facade::BaseManagerFacade;

// Bluetooth related includes
use crate::bluetooth::ble_advertise_facade::BleAdvertiseFacade;
use crate::bluetooth::bt_control_facade::BluetoothControlFacade;
use crate::bluetooth::gatt_client_facade::GattClientFacade;
use crate::bluetooth::gatt_server_facade::GattServerFacade;

use crate::bluetooth::facade::BluetoothFacade;
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

/// Sl4f stores state for all facades and has access to information for all connected clients.
///
/// To add support for a new Facade implementation, see the hashmap in `Sl4f::new`.
#[derive(Debug)]
pub struct Sl4f {
    // facades: Mapping of method prefix to object implementing that facade's API.
    facades: HashMap<String, Arc<dyn Facade>>,

    // connected clients
    clients: Arc<RwLock<Sl4fClients>>,
}

impl Sl4f {
    pub fn new(clients: Arc<RwLock<Sl4fClients>>) -> Result<Sl4f, Error> {
        fn to_arc_trait_object<'a, T: Facade + 'a>(facade: T) -> Arc<dyn Facade + 'a> {
            Arc::new(facade) as Arc<dyn Facade>
        }
        // To add support for a new facade, define a new submodule with the Facade implementation
        // and construct an instance and include it in the mapping below. The key is used to route
        // requests to the appropriate Facade. Facade constructors should generally not fail, as a
        // facade that returns an error here will prevent sl4f from starting.
        let facades = convert_args!(
            keys = String::from,
            values = to_arc_trait_object,
            hashmap!(
                "audio_facade" => AudioFacade::new()?,
                "basemgr_facade" => BaseManagerFacade::new(),
                "ble_advertise_facade" => BleAdvertiseFacade::new(),
                "bluetooth" => BluetoothFacade::new(),
                "bt_control_facade" => BluetoothControlFacade::new(),
                "factory_store_facade" => FactoryStoreFacade::new(),
                "file_facade" => FileFacade::new(),
                "gatt_client_facade" => GattClientFacade::new(),
                "gatt_server_facade" => GattServerFacade::new(),
                "logging_facade" => LoggingFacade::new(),
                "netstack_facade" => NetstackFacade::new(),
                "paver" => PaverFacade::new(),
                "profile_server_facade" => ProfileServerFacade::new(),
                "scenic_facade" => ScenicFacade::new(),
                "setui_facade" => SetUiFacade::new()?,
                "test_facade" => TestFacade::new(),
                "traceutil_facade" => TraceutilFacade::new(),
                "webdriver_facade" => WebdriverFacade::new(),
                "wlan" => WlanFacade::new()?,
            )
        );
        Ok(Sl4f { facades, clients })
    }

    /// Gets the facade registered with the given name, if one exists.
    pub fn get_facade(&self, name: &str) -> Option<Arc<dyn Facade>> {
        self.facades.get(name).map(Arc::clone)
    }

    /// Implement the Facade trait method cleanup() to clean up state when "/cleanup" is queried.
    pub fn cleanup(&self) {
        for facade in self.facades.values() {
            facade.cleanup();
        }
        self.clients.write().cleanup_clients();
    }

    pub fn print_clients(&self) {
        self.clients.read().print_clients();
    }

    /// Implement the Facade trait method print() to log state when "/print" is queried.
    pub fn print(&self) {
        for facade in self.facades.values() {
            facade.print();
        }
    }
}

/// Metadata for clients utilizing the /init API.
#[derive(Debug)]
pub struct Sl4fClients {
    // clients: map of clients that are connected to the sl4f server.
    // key = session_id (unique for every ACTS instance) and value = Data about client (see
    // sl4f_types.rs)
    clients: HashMap<String, Vec<ClientData>>,
}

impl Sl4fClients {
    pub fn new() -> Self {
        Self { clients: HashMap::new() }
    }

    /// Registers a new connected client. Returns true if the client was already initialized.
    fn init_client(&mut self, id: String) -> bool {
        use std::collections::hash_map::Entry::*;
        match self.clients.entry(id) {
            Occupied(entry) => {
                fx_log_warn!(tag: "client_init",
                    "Key: {:?} already exists in clients. ",
                    entry.key()
                );
                true
            }
            Vacant(entry) => {
                entry.insert(Vec::new());
                fx_log_info!(tag: "client_init", "Updated clients: {:?}", self.clients);
                false
            }
        }
    }

    fn store_response(&mut self, client_id: &str, command_response: ClientData) {
        match self.clients.get_mut(client_id) {
            Some(client_responses) => {
                client_responses.push(command_response);
                fx_log_info!(tag: "store_response", "Stored response. Updated clients: {:?}", self.clients);
            }
            None => {
                fx_log_err!(tag: "store_response", "Client doesn't exist in server database: {:?}", client_id)
            }
        }
    }

    fn cleanup_clients(&mut self) {
        self.clients.clear();
    }

    fn print_clients(&self) {
        fx_log_info!("SL4F Clients: {:?}", self.clients);
    }
}

/// Handles all incoming requests to SL4F server, routes accordingly
pub fn serve(
    request: &Request,
    clients: Arc<RwLock<Sl4fClients>>,
    rouille_sender: mpsc::UnboundedSender<AsyncRequest>,
) -> Response {
    router!(request,
        (GET) (/) => {
            // Parse the command request
            fx_log_info!(tag: "serve", "Received command request.");
            client_request(&clients, &request, &rouille_sender)
        },
        (GET) (/init) => {
            // Initialize a client
            fx_log_info!(tag: "serve", "Received init request.");
            client_init(&request, &clients)
        },
        (GET) (/print_clients) => {
            // Print information about all clients
            fx_log_info!(tag: "serve", "Received print client request.");
            const PRINT_ACK: &str = "Successfully printed clients.";
            clients.read().print_clients();
            rouille::Response::json(&PRINT_ACK)
        },
        (GET) (/cleanup) => {
            fx_log_info!(tag: "serve", "Received server cleanup request.");
            server_cleanup(&request, &rouille_sender)
        },
        _ => {
            fx_log_err!(tag: "serve", "Received unknown server request.");
            const FAIL_REQUEST_ACK: &str = "Unknown GET request.";
            let res = CommandResponse::new("".to_string(), None, serde::export::Some(FAIL_REQUEST_ACK.to_string()));
            rouille::Response::json(&res)
        }
    )
}

/// Given the request, map the test request to a FIDL query and execute
/// asynchronously
fn client_request(
    clients: &Arc<RwLock<Sl4fClients>>,
    request: &Request,
    rouille_sender: &mpsc::UnboundedSender<AsyncRequest>,
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
    let req = AsyncCommandRequest::new(
        async_sender,
        method_id.clone(),
        method_type,
        method_name,
        method_params,
    );
    rouille_sender
        .unbounded_send(AsyncRequest::Command(req))
        .expect("Failed to send request to async thread.");
    let resp: AsyncResponse = rouille_receiver.recv().unwrap();

    clients.write().store_response(&session_id, ClientData::new(method_id.clone(), resp.clone()));
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

/// Initializes a new client, adds to clients, a thread-safe HashMap
/// Returns a rouille::Response
fn client_init(request: &Request, clients: &Arc<RwLock<Sl4fClients>>) -> Response {
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

    if clients.write().init_client(client_id) {
        rouille::Response::json(&FAIL_INIT_ACK)
    } else {
        rouille::Response::json(&INIT_ACK)
    }
}

/// Given the name of the ACTS method, derive method type + method name
/// Returns two "", "" on invalid input which will later propagate to
/// method_to_fidl and raise an error
fn split_string(method_name_raw: String) -> (String, String) {
    let split = method_name_raw.split(COMMAND_DELIMITER);
    let string_split: Vec<&str> = split.collect();

    // Input must be two strings separated by "."
    if string_split.len() != COMMAND_SIZE {
        return ("".to_string(), "".to_string());
    };

    (string_split[0].to_string(), string_split[1].to_string())
}

/// Given a request, grabs the method id, name, and parameters
/// Return Sl4fError on error
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

fn server_cleanup(
    request: &Request,
    rouille_sender: &mpsc::UnboundedSender<AsyncRequest>,
) -> Response {
    const FAIL_CLEANUP_ACK: &str = "Failed to cleanup SL4F resources.";
    const CLEANUP_ACK: &str = "Successful cleanup of SL4F resources.";

    fx_log_info!(tag: "server_cleanup", "Cleaning up server state");
    let (_, method_id, _, _, _) = match parse_request(request) {
        Ok(res) => res,
        Err(_) => return Response::json(&FAIL_CLEANUP_ACK),
    };

    // Create channel for async thread to respond to
    let (async_sender, rouille_receiver) = std::sync::mpsc::channel();

    // Cleanup all resources associated with sl4f
    rouille_sender
        .unbounded_send(AsyncRequest::Cleanup(async_sender))
        .expect("Failed to send request to async thread.");
    let () = rouille_receiver.recv().expect("Async thread dropped responder.");

    let ack = CommandResponse::new(method_id, Some(json!(CLEANUP_ACK)), None);
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
