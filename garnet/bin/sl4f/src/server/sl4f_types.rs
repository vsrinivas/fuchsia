// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::{fmt::Debug, str::FromStr, sync::mpsc};
use thiserror::Error;

use crate::server::constants::{COMMAND_DELIMITER, COMMAND_SIZE};

/// An Sl4f facade that can handle incoming requests.
#[async_trait(?Send)]
pub trait Facade: Debug {
    /// Asynchronously handle the incoming request for the given method and arguments, returning a
    /// future object representing the pending operation.
    async fn handle_request(&self, method: String, args: Value) -> Result<Value, Error>;

    /// In response to a request to /cleanup, cleanup any cross-request state.
    fn cleanup(&self) {}

    /// In response to a request to /print, log relevant facade state.
    fn print(&self) {}
}

/// Information about each client that has connected
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ClientData {
    // client_id: String ID of client (ACTS test suite)
    pub command_id: Value,

    // command_result: The response of running the command (to be stored in the table)
    pub command_result: AsyncResponse,
}

impl ClientData {
    pub fn new(id: Value, result: AsyncResponse) -> ClientData {
        ClientData { command_id: id, command_result: result }
    }
}

/// The parsed `id` field from an incoming json-rpc request.
#[derive(Debug, PartialEq, Clone)]
pub struct RequestId {
    /// If the request ID is a string that contains a single '.', the text leading up to the '.' is
    /// extracted as the client identifier.
    client: Option<String>,

    /// The ID to send in the response.  If client is Some(_), this will be a substring of the
    /// request ID.
    id: Value,
}

impl RequestId {
    /// Parse a raw request ID into its session id (if present) and response id.
    pub fn new(raw: Value) -> Self {
        if let Some(s) = raw.as_str() {
            let parts = s.split('.').collect::<Vec<_>>();
            if parts.len() == 2 {
                return Self {
                    client: Some(parts[0].to_owned()),
                    id: Value::String(parts[1..].join(".")),
                };
            }
        }

        // If the raw ID wasn't a string that contained exactly 1 '.', pass it through to the
        // response unmodified.
        Self { client: None, id: raw }
    }

    /// Returns a reference to the session id, if present.
    pub fn session_id(&self) -> Option<&str> {
        self.client.as_ref().map(String::as_str)
    }

    /// Returns a reference to the response id.
    pub fn response_id(&self) -> &Value {
        &self.id
    }

    /// Returns the response id, consuming self.
    pub fn into_response_id(self) -> Value {
        self.id
    }
}

/// The parsed `method` field from an incoming json-rpc request.
#[derive(Debug, PartialEq, Eq, Clone, Default)]
pub struct MethodId {
    /// Method type of the request (e.g bluetooth, wlan, etc...)
    pub facade: String,

    /// Name of the method
    pub method: String,
}

impl FromStr for MethodId {
    type Err = MethodIdParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let parts = s.split(COMMAND_DELIMITER).collect::<Vec<_>>();

        if parts.len() != COMMAND_SIZE {
            return Err(MethodIdParseError(s.to_string()));
        }

        Ok(Self { facade: parts[0].to_string(), method: parts[1].to_string() })
    }
}

#[derive(Debug, PartialEq, Eq, Clone, Error)]
#[error("invalid method id: {}", _0)]
pub struct MethodIdParseError(String);

/// Required fields for making a request
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct CommandRequest {
    // method: name of method to be called
    pub method: String,

    // id: String id of command
    pub id: Value,

    // params: Arguments required for method
    pub params: Value,
}

/// Return packet after SL4F runs command
#[derive(Serialize, Clone, Debug)]
pub struct CommandResponse {
    // id: String id of command
    pub id: Value,

    // result: Result value of method call, can be None
    pub result: Option<Value>,

    // error: Error message of method call, can be None
    pub error: Option<String>,
}

impl CommandResponse {
    pub fn new(id: Value, result: Option<Value>, error: Option<String>) -> CommandResponse {
        CommandResponse { id, result, error }
    }
}

/// Represents a RPC request to be fulfilled by the FIDL event loop
#[derive(Debug)]
pub enum AsyncRequest {
    Cleanup(mpsc::Sender<()>),
    Command(AsyncCommandRequest),
}

/// Represents a RPC command request to be fulfilled by the FIDL event loop
#[derive(Debug)]
pub struct AsyncCommandRequest {
    // tx: Transmit channel from FIDL event loop to RPC request side
    pub tx: mpsc::Sender<AsyncResponse>,

    // method_id: struct containing:
    //  * facade: Method type of the request (e.g bluetooth, wlan, etc...)
    //  * method: Name of the method
    pub method_id: MethodId,

    // params: serde_json::Value representing args for method
    pub params: Value,
}

impl AsyncCommandRequest {
    pub fn new(
        tx: mpsc::Sender<AsyncResponse>,
        method_id: MethodId,
        params: Value,
    ) -> AsyncCommandRequest {
        AsyncCommandRequest { tx, method_id, params }
    }
}

/// Represents a RPC response from the FIDL event loop to the RPC request side
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AsyncResponse {
    // res: serde_json::Value of FIDL method result
    pub result: Option<Value>,

    pub error: Option<String>,
}

impl AsyncResponse {
    pub fn new(res: Result<Value, Error>) -> AsyncResponse {
        match res {
            Ok(v) => AsyncResponse { result: Some(v), error: None },
            Err(e) => AsyncResponse { result: None, error: Some(e.to_string()) },
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn parse_method_id_ok() {
        assert_eq!(
            "bt.send".parse(),
            Ok(MethodId { facade: "bt".to_string(), method: "send".to_string() })
        );
        assert_eq!(
            "FooFacade.BarMethod".parse(),
            Ok(MethodId { facade: "FooFacade".to_string(), method: "BarMethod".to_string() })
        );
        assert_eq!(
            "EmptyMethod.".parse(),
            Ok(MethodId { facade: "EmptyMethod".to_string(), method: "".to_string() })
        );
        assert_eq!(
            ".EmptyFacade".parse(),
            Ok(MethodId { facade: "".to_string(), method: "EmptyFacade".to_string() })
        );
    }

    #[test]
    fn parse_method_id_invalid() {
        fn assert_parse_error(s: &str) {
            assert_eq!(s.parse::<MethodId>(), Err(MethodIdParseError(s.to_string())));
        }

        // Invalid command (should result in empty result)
        assert_parse_error("bluetooth_send");

        // Too many separators in command
        assert_parse_error("wlan.scan.start");

        // Empty command
        assert_parse_error("");

        // No separator
        assert_parse_error("BluetoothSend");

        // Invalid separator
        assert_parse_error("Bluetooth,Scan");
    }

    #[test]
    fn parse_request_id_int() {
        let id = RequestId::new(json!(42));
        assert_eq!(id, RequestId { client: None, id: json!(42) });
        assert_eq!(id.session_id(), None);
        assert_eq!(id.response_id(), &json!(42));
        assert_eq!(id.into_response_id(), json!(42));
    }

    #[test]
    fn parse_request_id_single_str() {
        assert_eq!(RequestId::new(json!("123")), RequestId { client: None, id: json!("123") });
    }

    #[test]
    fn parse_request_id_too_many_dots() {
        assert_eq!(RequestId::new(json!("1.2.3")), RequestId { client: None, id: json!("1.2.3") });
    }

    #[test]
    fn parse_request_id_with_session_id() {
        let id = RequestId::new(json!("12.34"));
        assert_eq!(id, RequestId { client: Some("12".to_string()), id: json!("34") });
        assert_eq!(id.session_id(), Some("12"));
        assert_eq!(id.response_id(), &json!("34"));
        assert_eq!(id.into_response_id(), json!("34"));
    }
}
