// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async::{self as fasync, net::TcpListener},
    fuchsia_merkle::Hash,
    futures::prelude::*,
    hyper::{
        header,
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
        Body, Method, Request, Response, StatusCode,
    },
    parking_lot::Mutex,
    serde_json::json,
    std::{
        convert::Infallible,
        net::{Ipv4Addr, SocketAddr},
        str::FromStr,
        sync::Arc,
    },
};

#[derive(Copy, Clone, Debug)]
pub enum OmahaResponse {
    NoUpdate,
    Update,
    InvalidResponse,
    InvalidURL,
}

/// A mock Omaha server.
#[derive(Clone, Debug)]
pub struct OmahaServer {
    inner: Arc<Mutex<Inner>>,
}

/// Shared state.
#[derive(Clone, Debug)]
struct Inner {
    response: OmahaResponse,
    merkle: Hash,
    update_check: Option<UpdateCheckAssertion>,
}

#[derive(Debug)]
pub struct OmahaServerBuilder {
    inner: Inner,
}

#[derive(Copy, Clone, Debug)]
pub enum UpdateCheckAssertion {
    UpdatesEnabled,
    UpdatesDisabled,
}

impl OmahaServerBuilder {
    /// Sets the server's response to update checks
    pub fn response(mut self, response: OmahaResponse) -> Self {
        self.inner.response = response;
        self
    }

    /// Sets the merkle of the update package for an Update response
    pub fn merkle(mut self, merkle: Hash) -> Self {
        self.inner.merkle = merkle;
        self
    }

    /// Sets the special assertion to make on all update check requests
    pub fn update_check_assertion(mut self, value: Option<UpdateCheckAssertion>) -> Self {
        self.inner.update_check = value;
        self
    }

    /// Constructs the OmahaServer
    pub fn build(self) -> OmahaServer {
        OmahaServer { inner: Arc::new(Mutex::new(self.inner)) }
    }
}

impl OmahaServer {
    /// Returns an OmahaServer builder with the following defaults:
    /// * response: NoUpdate
    /// * merkle: 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef
    /// * no special request assertions beyond the defaults
    pub fn builder() -> OmahaServerBuilder {
        OmahaServerBuilder {
            inner: Inner {
                response: OmahaResponse::NoUpdate,
                merkle: Hash::from_str(
                    "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                )
                .unwrap(),
                update_check: None,
            },
        }
    }

    pub fn new(response: OmahaResponse) -> Self {
        Self::builder().response(response).build()
    }

    pub fn new_with_hash(response: OmahaResponse, merkle: Hash) -> Self {
        Self::builder().response(response).merkle(merkle).build()
    }

    /// Sets the special assertion to make on any future update check requests
    pub fn set_update_check_assertion(&self, value: Option<UpdateCheckAssertion>) {
        self.inner.lock().update_check = value;
    }

    /// Spawn the server on the current executor, returning the address of the server.
    pub fn start(self) -> Result<String, Error> {
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);

        let (connections, addr) = {
            let listener = TcpListener::bind(&addr)?;
            let local_addr = listener.local_addr()?;
            (
                listener
                    .accept_stream()
                    .map_ok(|(conn, _addr)| fuchsia_hyper::TcpStream { stream: conn }),
                local_addr,
            )
        };

        let inner = Arc::clone(&self.inner);
        let make_svc = make_service_fn(move |_socket| {
            let inner = Arc::clone(&inner);
            async move {
                Ok::<_, Infallible>(service_fn(move |req| {
                    let inner = Arc::clone(&inner);
                    async move { handle_omaha_request(req, &*inner).await }
                }))
            }
        });

        let server = Server::builder(from_stream(connections))
            .executor(fuchsia_hyper::Executor)
            .serve(make_svc)
            .unwrap_or_else(|e| panic!("error serving omaha server: {}", e));

        fasync::Task::spawn(server).detach();

        Ok(format!("http://{}/", addr))
    }
}

async fn handle_omaha_request(
    req: Request<Body>,
    inner: &Mutex<Inner>,
) -> Result<Response<Body>, Error> {
    let inner = inner.lock().clone();

    assert_eq!(req.method(), Method::POST);
    assert_eq!(req.uri().query(), None);

    let req_body = req
        .into_body()
        .try_fold(Vec::new(), |mut vec, b| async move {
            vec.extend(b);
            Ok(vec)
        })
        .await?;
    let req_json: serde_json::Value = serde_json::from_slice(&req_body).expect("parse json");

    let request = req_json.get("request").unwrap();
    let apps = request.get("app").unwrap().as_array().unwrap();
    assert_eq!(apps.len(), 1);
    let app = &apps[0];
    let appid = app.get("appid").unwrap();
    assert_eq!(appid, "integration-test-appid");
    let version = app.get("version").unwrap();
    assert_eq!(version, "0.1.2.3");

    let package_name = format!("update?hash={}", inner.merkle);
    let app = if let Some(update_check) = app.get("updatecheck") {
        let updatedisabled =
            update_check.get("updatedisabled").map(|v| v.as_bool().unwrap()).unwrap_or(false);
        match inner.update_check {
            Some(UpdateCheckAssertion::UpdatesEnabled) => {
                assert!(!updatedisabled);
            }
            Some(UpdateCheckAssertion::UpdatesDisabled) => {
                assert!(updatedisabled);
            }
            None => {}
        }

        let updatecheck = match inner.response {
            OmahaResponse::Update => json!({
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
                                "run": &package_name,
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
                                "name": &package_name,
                                "fp": "2.0.1.2.3",
                                "required": true
                            }
                        ]
                    }
                }
            }),
            OmahaResponse::NoUpdate => json!({
                "status": "noupdate",
            }),
            OmahaResponse::InvalidResponse => json!({
                "invalid_status": "invalid",
            }),
            OmahaResponse::InvalidURL => json!({
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
                                "run": &package_name,
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
                                "name": &package_name,
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Context,
        hyper::{Body, StatusCode},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_server_replies() -> Result<(), Error> {
        let server =
            OmahaServer::new(OmahaResponse::NoUpdate).start().context("starting server")?;

        let client = fuchsia_hyper::new_client();
        let body = json!({
            "request": {
                "app": [
                    {
                        "appid": "integration-test-appid",
                        "version": "0.1.2.3",
                        "updatecheck": { "updatedisabled": false }
                    }
                ]
            }
        });
        let request = Request::post(server).body(Body::from(body.to_string())).unwrap();

        let response = client.request(request).await?;

        assert_eq!(response.status(), StatusCode::OK);
        let body = response
            .into_body()
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await
            .context("reading response body")?;
        let obj: serde_json::Value =
            serde_json::from_slice(&body).context("parsing response json")?;

        let response = obj.get("response").unwrap();
        let apps = response.get("app").unwrap().as_array().unwrap();
        assert_eq!(apps.len(), 1);
        let app = &apps[0];
        let status = app.get("updatecheck").unwrap().get("status").unwrap();
        assert_eq!(status, "noupdate");
        Ok(())
    }
}
