// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bt::error::Error as BTError;
use failure::Error;
use futures::channel::mpsc;
use parking_lot::{Mutex, RwLock};
use rouille;
use rouille::{Request, Response};
use serde;
use serde_json;
use serde_json::{to_value, Value};
use std;
use std::collections::HashMap;
use std::io::Read;
use std::sync::Arc;

// Standardized sl4f types and constants
use server::constants::{COMMAND_DELIMITER, COMMAND_SIZE};
use server::sl4f_types::{AsyncRequest, AsyncResponse, ClientData, CommandRequest, CommandResponse};

// Bluetooth related includes (do the same for each connectivity stack)
use bluetooth::facade::BluetoothFacade;

/// Sl4f object. This stores all information about state for each connectivity stack.
/// Every session will have a new Sl4f object.
/// For example, to add WLAN stack support, add "wlan_facade" to the struct definition and update
/// the impl functions. Then, update method_to_fidl() to support the "wlan" method type.
#[derive(Debug, Clone)]
pub struct Sl4f {
    // bt_facade: Thread safe object for state for bluetooth connectivity tests
    bt_facade: Arc<RwLock<BluetoothFacade>>,

    // clients: Thread safe map for clients that are connected to the sl4f server.
    // key = session_id (unique for every ACTS instance) and value = Data about client (see
    // sl4f_types.rs)
    clients: Arc<Mutex<HashMap<String, Vec<ClientData>>>>,
}

impl Sl4f {
    pub fn new() -> Arc<RwLock<Sl4f>> {
        Arc::new(RwLock::new(Sl4f {
            bt_facade: BluetoothFacade::new(None, None),
            clients: Arc::new(Mutex::new(HashMap::new())),
        }))
    }

    pub fn get_bt_facade(&self) -> Arc<RwLock<BluetoothFacade>> {
        self.bt_facade.clone()
    }

    pub fn get_clients(&self) -> Arc<Mutex<HashMap<String, Vec<ClientData>>>> {
        self.clients.clone()
    }

    pub fn cleanup_clients(&mut self) {
        self.clients.lock().clear();
    }

    pub fn cleanup(&mut self) {
        BluetoothFacade::cleanup(self.bt_facade.clone());
        self.cleanup_clients();
    }

    pub fn print_clients(&self) {
        fx_log_info!("SL4F Clients: {:?}", self.clients);
    }

    // Add *_facade.print() when new Facade objects are added (i.e WlanFacade)
    pub fn print(&self) {
        self.bt_facade.read().print();
    }
}

// Handles all incoming requests to SL4F server, routes accordingly
pub fn serve(
    request: &Request, sl4f_session: Arc<RwLock<Sl4f>>,
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
    sl4f_session: Arc<RwLock<Sl4f>>, client_id: String, method_id: String, result: AsyncResponse,
) {
    let clients = sl4f_session.write().clients.clone();

    // If the current client session is found, append the result of the FIDL call to the result
    // history
    if clients.lock().contains_key(&client_id) {
        let command_response = ClientData::new(method_id.clone(), result.clone());
        clients
            .lock()
            .entry(client_id.clone())
            .or_insert(Vec::new())
            .push(command_response);
    } else {
        fx_log_err!(tag: "store_response", "Client doesn't exist in server database: {:?}", client_id);
    }

    fx_log_info!(tag: "store_response", "Stored response. Updated clients: {:?}", clients);
}

// Given the request, map the test request to a FIDL query and execute
// asynchronously
fn client_request(
    sl4f_session: Arc<RwLock<Sl4f>>, request: &Request,
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
    let req = AsyncRequest::new(
        async_sender,
        method_id.clone(),
        method_type,
        method_name,
        method_params,
    );
    rouille_sender
        .unbounded_send(req)
        .expect("Failed to send request to async thread.");
    let resp: AsyncResponse = rouille_receiver.recv().unwrap();

    store_response(
        sl4f_session,
        session_id.clone(),
        method_id.clone(),
        resp.clone(),
    );
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
    request: &Request, clients: Arc<Mutex<HashMap<String, Vec<ClientData>>>>,
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
// Return BTError if fail
fn parse_request(request: &Request) -> Result<(String, String, String, String, Value), Error> {
    let mut data = match request.data() {
        Some(d) => d,
        None => return Err(BTError::new("Failed to parse request buffer.").into()),
    };

    let mut buf: String = String::new();
    if data.read_to_string(&mut buf).is_err() {
        return Err(BTError::new("Failed to read request buffer.").into());
    }

    // Ignore the json_rpc field
    let request_data: CommandRequest = match serde_json::from_str(&buf) {
        Ok(tdata) => tdata,
        Err(_) => return Err(BTError::new("Failed to unpack request data.").into()),
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
    Ok((
        session_id,
        method_id,
        method_type,
        method_name,
        method_params,
    ))
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
