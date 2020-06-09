// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::engine::dispatcher::{ControllerDispatcher, DispatcherError},
    log::{info, warn},
    rouille::{Response, ResponseBody},
    serde_json::json,
    std::sync::{Arc, RwLock},
};

pub fn run(dispatcher: Arc<RwLock<ControllerDispatcher>>) {
    info!("Server starting: http://0.0.0.0:8080");
    rouille::start_server("0.0.0.0:8080", move |request| {
        info!("Request: {}", request.url());
        // Run the query.
        let dispatch = dispatcher.read().unwrap();
        // TODO(benwright) - Pass in the request body.
        match dispatch.query(request.url(), json!("")) {
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
                let result = json!({
                    "status": "error",
                    "description": e.to_string()
                });
                Response {
                    status_code: 500,
                    headers: vec![("Content-Type".into(), "application/json".into())],
                    data: ResponseBody::from_string(result.to_string()),
                    upgrade: None,
                }
            }
        }
    });
}
