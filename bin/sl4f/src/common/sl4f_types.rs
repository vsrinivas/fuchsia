// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use serde_json::Value;
use std::sync::mpsc;

/// Information about each client that has connected
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ClientData {
    // client_id: String ID of client (ACTS test suite)
    pub client_id: String,
}

/// Required fields for making a request
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct CommandRequest {
    // method: name of method to be called
    pub method: String,

    // id: Integer id of command
    pub id: u32,

    // params: Arguments required for method
    pub params: Value,
}

/// Return packet after SL4F runs command
#[derive(Serialize, Debug)]
pub struct CommandResponse {
    // id: Integer id of command
    pub id: u32,

    // result: Result value of method call, can be None
    pub result: Option<Value>,

    // error: Error message of method call, can be None
    pub error: Option<String>,
}

impl CommandResponse {
    pub fn new(id: u32, result: Option<Value>, error: Option<String>) -> CommandResponse {
        CommandResponse { id, result, error }
    }
}

// Represents a RPC request to be fulfilled by the FIDL event loop
#[derive(Debug)]
pub struct AsyncRequest {
    // tx: Transmit channel from FIDL event loop to RPC request side
    pub tx: mpsc::Sender<AsyncResponse>,

    // id: Integer id of the method
    pub id: u32,

    // type: Method type of the request (e.g bluetooth, wlan, etc...)
    pub method_type: String,

    // name: Name of the method
    pub name: String,

    // params: serde_json::Value representing args for method
    pub params: Value,
}

impl AsyncRequest {
    pub fn new(
        tx: mpsc::Sender<AsyncResponse>, id: u32, method_type: String, name: String, params: Value,
    ) -> AsyncRequest {
        AsyncRequest {
            tx,
            id,
            method_type,
            name,
            params,
        }
    }
}

// Represents a RPC response from the FIDL event loop to the RPC request side
#[derive(Debug)]
pub struct AsyncResponse {
    // res: serde_json::Value of FIDL method result
    pub res: Result<Value, Error>,
}

impl AsyncResponse {
    pub fn new(res: Result<Value, Error>) -> AsyncResponse {
        AsyncResponse { res }
    }
}

/// Enum for supported connectivity stacks
/// Make sure to update sl4f.rs:method_to_fidl() match statement
pub enum FacadeType {
    Bluetooth,
    Wlan,
    Undefined,
}

impl FacadeType {
    pub fn from_str(facade: String) -> FacadeType {
        match facade.as_ref() {
            "bluetooth" => FacadeType::Bluetooth,
            "wlan" => FacadeType::Wlan,
            _ => FacadeType::Undefined,
        }
    }
}
