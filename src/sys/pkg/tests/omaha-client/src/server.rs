// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async::{self as fasync, net::TcpListener, EHandle},
    futures::{
        compat::{Future01CompatExt, Stream01CompatExt},
        prelude::*,
        task::SpawnExt,
    },
    hyper::{header, service::service_fn, Body, Method, Request, Response, Server, StatusCode},
    serde_json::json,
    std::net::{Ipv4Addr, SocketAddr},
};

#[derive(Copy, Clone, Debug)]
pub enum OmahaReponse {
    Update,
    NoUpdate,
    InvalidResponse,
    InvalidURL,
}

/// A mock Omaha server.
pub struct OmahaServer {
    response: OmahaReponse,
}

impl OmahaServer {
    pub fn new(response: OmahaReponse) -> Self {
        OmahaServer { response }
    }

    /// Spawn the server on the current executor, returning the address of the server.
    pub fn start(self) -> Result<String, Error> {
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);

        let (connections, addr) = {
            let listener = TcpListener::bind(&addr)?;
            let local_addr = listener.local_addr()?;
            (listener.accept_stream().map_ok(|(conn, _addr)| conn.compat()), local_addr)
        };

        let response = self.response;
        let service =
            move || service_fn(move |req| handle_omaha_request(req, response).boxed().compat());

        let server = Server::builder(connections.compat())
            .executor(EHandle::local().compat())
            .serve(service)
            .compat()
            .unwrap_or_else(|e| panic!("error serving omaha server: {}", e));

        fasync::spawn(server);

        Ok(format!("http://{}/", addr))
    }
}

async fn handle_omaha_request(
    req: Request<Body>,
    response: OmahaReponse,
) -> Result<Response<Body>, Error> {
    assert_eq!(req.method(), Method::POST);
    assert_eq!(req.uri().query(), None);

    let req_body = req.into_body().compat().try_concat().await.unwrap().to_vec();
    let req_json: serde_json::Value = serde_json::from_slice(&req_body).expect("parse json");

    let request = req_json.get("request").unwrap();
    let apps = request.get("app").unwrap().as_array().unwrap();
    assert_eq!(apps.len(), 1);
    let app = &apps[0];
    let appid = app.get("appid").unwrap();
    assert_eq!(appid, "integration-test-appid");
    let version = app.get("version").unwrap();
    assert_eq!(version, "0.1.2.3");

    let is_update_check = app.get("updatecheck").is_some();
    let app = if is_update_check {
        let updatecheck = match response {
            OmahaReponse::Update => json!({
                "status": "ok",
                "urls": {
                    "url": [
                        {
                            "codebase": "fuchsia-pkg://integration.test.fuchsia.com/"
                        }
                    ]
                },
                "manifest": {
                    "version": "0.1.2.3",
                    "actions": {
                        "action": [
                            {
                                "run": "update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                "event": "install"
                            },
                            {
                                "event": "postinstall"
                            }
                        ]
                    },
                    "packages": {
                        "package": [
                            {
                                "name": "update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                "fp": "2.0.1.2.3",
                                "required": true
                            }
                        ]
                    }
                }
            }),
            OmahaReponse::NoUpdate => json!({
                "status": "noupdate",
            }),
            OmahaReponse::InvalidResponse => json!({
                "invalid_status": "invalid",
            }),
            OmahaReponse::InvalidURL => json!({
                "status": "ok",
                "urls": {
                    "url": [
                        {
                            "codebase": "http://integration.test.fuchsia.com/"
                        }
                    ]
                },
                "manifest": {
                    "version": "0.1.2.3",
                    "actions": {
                        "action": [
                            {
                                "run": "update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                "event": "install"
                            },
                            {
                                "event": "postinstall"
                            }
                        ]
                    },
                    "packages": {
                        "package": [
                            {
                                "name": "update?hash=deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                                "fp": "2.0.1.2.3",
                                "required": true
                            }
                        ]
                    }
                }
            }),
        };
        json!(
        {
            "cohorthint": "integration-test",
            "appid": appid,
            "cohort": "1:1:",
            "status": "ok",
            "cohortname": "integration-test",
            "updatecheck": updatecheck,
        })
    } else {
        assert!(app.get("event").is_some());
        json!(
        {
            "cohorthint": "integration-test",
            "appid": appid,
            "cohort": "1:1:",
            "status": "ok",
            "cohortname": "integration-test",
        })
    };
    let response = json!({
        "response": {
            "server": "prod",
            "protocol": "3.0",
            "daystart": {
                "elapsed_seconds": 48810,
                "elapsed_days": 4775
            },
            "app": [
                app
            ]
        }
    });

    let data = serde_json::to_vec(&response).unwrap();

    Ok(Response::builder()
        .status(StatusCode::OK)
        .header(header::CONTENT_LENGTH, data.len())
        .body(Body::from(data))
        .unwrap())
}
