// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_json::Value;

// Information about each client that has connected
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ClientData {
    // client_id: String ID of client (ACTS test suite)
    pub client_id: String,
}

// Required fields for making a request
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct CommandRequest {
    // method: name of method to be called
    pub method: String,

    // id: Integer id of command
    pub id: u32,

    // params: Arguments required for method
    pub params: Value,
}

// TODO(aniramakri): Add support for proper error handling over JSON RPC
// Return packet after SL4F runs command
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
        CommandResponse {
            id: id,
            result: result,
            error: error,
        }
    }
}
