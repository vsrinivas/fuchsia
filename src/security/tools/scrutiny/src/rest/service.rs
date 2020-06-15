// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::engine::dispatcher::{ControllerDispatcher, DispatcherError},
    anyhow::Error,
    log::{info, warn},
    rouille::{Response, ResponseBody},
    serde_json::json,
    std::io::Read,
    std::sync::{Arc, RwLock},
};

/// Converts a rust error into a JSON error response.
fn error_response(error: Error) -> Response {
    let result = json!({
        "status": "error",
        "description": error.to_string(),
    });
    Response {
        status_code: 500,
        headers: vec![("Content-Type".into(), "application/json".into())],
        data: ResponseBody::from_string(result.to_string()),
        upgrade: None,
    }
}

/// Runs the core REST service loop, parsing URLs and queries to their
/// respective controllers via the ControllerDispatcher. This function does
/// not exit.
pub fn run(dispatcher: Arc<RwLock<ControllerDispatcher>>, port: u16) {
    let addr = format!("0.0.0.0:{}", port);
    info!("Server starting: http://{}", addr);
    rouille::start_server(addr, move |request| {
        info!("Request: {}", request.url());
        let mut body = request.data().expect("RequestBody already retrieved");
        let mut query = String::new();
        if let Err(e) = body.read_to_string(&mut query) {
            warn!("Failed to read request body.");
            return error_response(Error::new(e));
        }

        let dispatch = dispatcher.read().unwrap();
        match dispatch.query(request.url(), json!(query)) {
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
                error_response(e)
            }
        }
    });
}
