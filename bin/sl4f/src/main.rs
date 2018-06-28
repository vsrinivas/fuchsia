// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_bluetooth as fidl_bt;
extern crate fidl_fuchsia_bluetooth_le as fidl_ble;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_bluetooth as bt;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate parking_lot;
#[macro_use]
extern crate rouille;
extern crate serde;
extern crate serde_json;
#[macro_use]
extern crate serde_derive;

use parking_lot::Mutex;
use rouille::{Request, Response};
use serde_json::Value;
use std::collections::HashMap;
use std::io::Read;
use std::sync::Arc;

mod common;
use common::bluetooth_commands::convert_to_fidl;

// Information about each client that has connected
#[derive(Serialize, Deserialize, Debug, Clone)]
struct ClientData {
    // device: string name of Fuchsia device
    device: String,

    // host_address: String IP of Fuchsia device
    host_address: String,

    // client_id: String ID of client (ACTS test suite)
    client_id: String,
}

// Required fields for making a request
#[derive(Serialize, Deserialize, Debug, Clone)]
struct CommandRequest {
    // method: name of method to be called
    method: String,

    // id: Integer id of command
    id: u32,

    // params: Arguments required for method
    params: Value,
}

// TODO(aniramakri): Add support for proper error handling over JSON RPC
// Return packet after SL4F runs command
#[derive(Serialize, Debug)]
struct CommandResponse {
    // id: Integer id of command
    id: u32,

    // result: Result value of method call, can be None
    result: Option<Value>,

    // error: Error message of method call, can be None
    error: Option<String>,
}

impl CommandResponse {
    fn new(id: u32, result: Option<Value>, error: Option<String>) -> CommandResponse {
        CommandResponse {
            id: id,
            result: result,
            error: error,
        }
    }
}

// Config, flexible for any ip/port combination
const SERVER_IP: &str = "0.0.0.0";
const SERVER_PORT: &str = "80";

// Skeleton of HTTP server using rouille
fn main() {
    let address = format!("{}:{}", SERVER_IP, SERVER_PORT);

    eprintln!("Now listening on: {}", address);

    let clients: Arc<Mutex<HashMap<String, ClientData>>> = Arc::new(Mutex::new(HashMap::new()));

    // Start listening on address
    rouille::start_server(address, move |request| {
        handle_traffic(&request, clients.clone())
    });
}

// Handles all incoming requests to SL4F server, routes accordingly
fn handle_traffic(request: &Request, clients: Arc<Mutex<HashMap<String, ClientData>>>) -> Response {
    router!(request,
            (GET) (/) => {
                // Parse the command request
                eprintln!("Command request.");
                test_request(&request)
            },
            (GET) (/init) => {
                // Initialize a client
                eprintln!("Init request.");
                client_init(&request, clients.clone())
            },
            (GET) (/print_clients) => {
                // Print information about all clients
                eprintln!("Received request for printing clients.");
                const PRINT_ACK: &str = "Successfully printed clients.";
                rouille::Response::json(&PRINT_ACK)
            },
            _ => {
                // TODO(aniramakri): Better error handling for unkown queries
                const FAIL_REQUEST_ACK: &str = "Unknown GET request. Aborting.";
                let res = CommandResponse::new(0, None, serde::export::Some(FAIL_REQUEST_ACK.to_string()));
                rouille::Response::json(&res)
            }
        )
}

// Given the request, map the test request to a FIDL query and execute
// asynchronously
fn test_request(request: &Request) -> Response {
    const FAIL_TEST_ACK: &str = "Command failed";

    let mut data = match request.data() {
        Some(d) => d,
        None => return Response::json(&FAIL_TEST_ACK),
    };

    let mut buf: String = String::new();
    if data.read_to_string(&mut buf).is_err() {
        return Response::json(&FAIL_TEST_ACK);
    }

    let request_data: CommandRequest = match serde_json::from_str(&buf) {
        Ok(tdata) => tdata,
        Err(_) => return Response::json(&FAIL_TEST_ACK),
    };

    let method_id = request_data.id.clone();
    let method_name = request_data.method.clone();
    let method_params = request_data.params.clone();
    eprintln!(
        "method id: {:?}, name: {:?}, args: {:?}",
        method_id, method_name, method_params
    );

    let fidl_response = convert_to_fidl(method_name, method_params);
    eprintln!("Recieved fidl method response: {:?}", fidl_response);

    // TODO(aniramakri): Add better error descriptions
    match fidl_response {
        Ok(r) => {
            let res = CommandResponse::new(method_id, Some(serde_json::to_value(r).unwrap()), None);
            rouille::Response::json(&res)
        }
        Err(_) => {
            let res = CommandResponse::new(
                method_id,
                None,
                serde::export::Some(FAIL_TEST_ACK.to_string()),
            );
            rouille::Response::json(&res)
        }
    }
}

// Initializes a new client, adds to clients, a thread-safe HashMap
// Returns a rouille::Response
fn client_init(request: &Request, clients: Arc<Mutex<HashMap<String, ClientData>>>) -> Response {
    const INIT_ACK: &str = "Recieved init request.";
    const FAIL_INIT_ACK: &str = "Failed to init client.";

    let mut data = match request.data() {
        Some(d) => d,
        None => return Response::json(&FAIL_INIT_ACK),
    };

    let mut buf: String = String::new();
    if data.read_to_string(&mut buf).is_err() {
        return Response::json(&FAIL_INIT_ACK);
    }

    let curr_client_data: ClientData = match serde_json::from_str(&buf) {
        Ok(cdata) => cdata,
        Err(_) => return Response::json(&FAIL_INIT_ACK),
    };

    let curr_client_id: String = curr_client_data.client_id.clone();

    if clients.lock().contains_key(&curr_client_id) {
        eprintln!(
            "handle_init error! Key: {:?} already exists in clients. ",
            curr_client_id
        );
        return rouille::Response::json(&FAIL_INIT_ACK);
    }

    clients.lock().insert(curr_client_id, curr_client_data);

    rouille::Response::json(&INIT_ACK)
}
