// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::engine::dispatcher::{ControllerDispatcher, DispatcherError},
    anyhow::Error,
    log::{info, warn},
    rouille::{Response, ResponseBody},
    serde_json::json,
    std::collections::HashMap,
    std::io::{self, ErrorKind, Read},
    std::sync::{Arc, RwLock},
};

/// Converts a rust error into a JSON error response.
fn error_response(error: Error, status_code: Option<u16>) -> Response {
    let result = json!({
        "status": "error",
        "description": error.to_string(),
    });

    Response {
        status_code: {
            if let Some(code) = status_code {
                code
            } else {
                500
            }
        },
        headers: vec![("Content-Type".into(), "application/json".into())],
        data: ResponseBody::from_string(result.to_string()),
        upgrade: None,
    }
}

fn parse_get_params(query_str: &str) -> HashMap<String, String> {
    // TODO: Sanitize these values.
    query_str
        .split('&')
        .map(|kv| {
            if let Some(ind) = kv.find('=') {
                if ind == 0 || ind == kv.len() - 1 {
                    // If the = is at the end of the split, ignore.
                    None
                } else {
                    // This only works for string-string kv pairs.
                    let (key, value) = kv.split_at(ind);
                    Some((key.to_string(), value[1..].to_string()))
                }
            } else {
                // If we cannot find a =, ignore
                None
            }
        })
        .flatten()
        .collect()
}

/// Runs the core REST service loop, parsing URLs and queries to their
/// respective controllers via the ControllerDispatcher. This function does
/// not exit.
pub fn run(dispatcher: Arc<RwLock<ControllerDispatcher>>, port: u16) {
    let addr = format!("0.0.0.0:{}", port);
    info!("Server starting: http://{}", addr);
    rouille::start_server(addr, move |request| {
        let method = request.method();
        info!("Request: {} {}", method, request.url());
        let mut body = request.data().expect("RequestBody already retrieved");

        let query_val = match method {
            "GET" => {
                // TODO: Looking at the source for `get_param(&self, param_name: &str)` seems like it's not great to
                // rely on that function since it doesn't match against the entire parameter name...
                let query = request.raw_query_string();
                let params = parse_get_params(query);
                Ok(json!(params))
            }
            "POST" => {
                let mut query = String::new();
                if let Err(e) = body.read_to_string(&mut query) {
                    warn!("Failed to read request body.");
                    return error_response(Error::new(e), Some(400));
                }
                serde_json::from_str(&query)
            }
            _ => {
                // FIXME: Technically we should always be serving HEAD requests.
                warn!("Expected GET or POST method, received {}.", method);
                return error_response(
                    Error::new(io::Error::new(ErrorKind::ConnectionRefused, "Unsupported method.")),
                    Some(405),
                );
            }
        };

        let dispatch = dispatcher.read().unwrap();
        if let Ok(json_val) = query_val {
            match dispatch.query(request.url(), json_val) {
                Ok(result) => Response {
                    status_code: 200,
                    headers: vec![("Content-Type".into(), "application/json".into())],
                    data: ResponseBody::from_string(result.to_string()),
                    upgrade: None,
                },
                Err(e) => {
                    if let Some(dispatch_error) = e.downcast_ref::<DispatcherError>() {
                        if let DispatcherError::NamespaceDoesNotExist(_) = dispatch_error {
                            warn!("Address not found.");
                            return Response::empty_404();
                        }
                    }
                    error_response(e, None)
                }
            }
        } else {
            return Response::empty_400();
        }
    });
}
