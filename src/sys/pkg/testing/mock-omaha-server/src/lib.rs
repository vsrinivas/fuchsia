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
    itertools::zip_eq,
    parking_lot::Mutex,
    serde_json::json,
    std::{
        convert::Infallible,
        net::{Ipv4Addr, SocketAddr},
        str::FromStr,
        sync::Arc,
    },
};

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
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

#[derive(Clone, Debug)]
pub struct ResponseAndMetadata {
    pub response: OmahaResponse,
    pub merkle: Hash,
    pub check_assertion: Option<UpdateCheckAssertion>,
    pub app_id: String,
    pub version: String,
}

impl Default for ResponseAndMetadata {
    fn default() -> ResponseAndMetadata {
        ResponseAndMetadata {
            response: OmahaResponse::NoUpdate,
            merkle: Hash::from_str(
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
            )
            .unwrap(),
            check_assertion: None,
            app_id: "integration-test-appid".to_string(),
            version: "0.1.2.3".to_string(),
        }
    }
}

/// Shared state.
#[derive(Clone, Debug)]
struct Inner {
    responses_and_metadata: Vec<ResponseAndMetadata>,
}

impl Inner {
    pub fn num_apps(&self) -> usize {
        self.responses_and_metadata.len()
    }
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
    pub fn set(mut self, responses_and_metadata: Vec<ResponseAndMetadata>) -> Self {
        self.inner.responses_and_metadata = responses_and_metadata;
        self
    }

    /// Constructs the OmahaServer
    pub fn build(self) -> OmahaServer {
        OmahaServer { inner: Arc::new(Mutex::new(self.inner)) }
    }
}

impl OmahaServer {
    /// Returns an OmahaServer builder with the following defaults:
    /// * response: [NoUpdate]
    /// * merkle: [0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef]
    /// * no special request assertions beyond the defaults
    pub fn builder() -> OmahaServerBuilder {
        OmahaServerBuilder {
            inner: Inner { responses_and_metadata: vec![ResponseAndMetadata::default()] },
        }
    }

    pub fn new(responses: Vec<OmahaResponse>) -> Self {
        Self::builder()
            .set(
                responses
                    .iter()
                    .map(|response| ResponseAndMetadata {
                        response: *response,
                        ..Default::default()
                    })
                    .collect(),
            )
            .build()
    }

    pub fn new_with_hash(responses: Vec<OmahaResponse>, merkles: Vec<Hash>) -> Self {
        Self::builder()
            .set(
                zip_eq(responses, merkles)
                    .map(|(response, merkle)| ResponseAndMetadata {
                        response,
                        merkle,
                        ..Default::default()
                    })
                    .collect(),
            )
            .build()
    }

    pub fn new_with_metadata(responses_and_metadata: Vec<ResponseAndMetadata>) -> Self {
        Self::builder().set(responses_and_metadata).build()
    }

    /// Sets the special assertion to make on any future update check requests
    pub fn set_all_update_check_assertions(&self, value: Option<UpdateCheckAssertion>) {
        for response_and_metadata in self.inner.lock().responses_and_metadata.iter_mut() {
            response_and_metadata.check_assertion = value;
        }
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

    // If this request contains updatecheck, make sure the mock has the right number of configured apps.
    match apps.iter().filter(|app| app.get("updatecheck").is_some()).count() {
        0 => {}
        x => assert_eq!(x, inner.num_apps()),
    }

    let apps: Vec<serde_json::Value> = apps
        .iter()
        .map(|app| {
            let appid = app.get("appid").unwrap();
            let expected =
                inner.responses_and_metadata.iter().find(|r| &r.app_id == appid).unwrap();

            let version = app.get("version").unwrap();
            assert_eq!(version, &expected.version);

            let package_name = format!("update?hash={}", expected.merkle);
            let app = if let Some(expected_update_check) = app.get("updatecheck") {
                let updatedisabled = expected_update_check
                    .get("updatedisabled")
                    .map(|v| v.as_bool().unwrap())
                    .unwrap_or(false);
                match expected.check_assertion {
                    Some(UpdateCheckAssertion::UpdatesEnabled) => {
                        assert!(!updatedisabled);
                    }
                    Some(UpdateCheckAssertion::UpdatesDisabled) => {
                        assert!(updatedisabled);
                    }
                    None => {}
                }

                let updatecheck = match expected.response {
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
            app
        })
        .collect();
    let response = json!({
        "response": {
            "server": "prod",
            "protocol": "3.0",
            "daystart": {
                "elapsed_seconds": 48810,
                "elapsed_days": 4775
            },
            "app": apps
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
    #[should_panic(expected = "reached end of one iterator before the other")]
    // TODO(fxbug.dev/88496): delete the below
    #[cfg_attr(feature = "variant_asan", ignore)]
    async fn test_invalid_construction() {
        // This is invalid because len(responses) == 2, but len(merkles) == 1.
        let _ = OmahaServer::new_with_hash(
            vec![OmahaResponse::NoUpdate, OmahaResponse::NoUpdate],
            vec![Hash::from_str(
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
            )
            .unwrap()],
        )
        .start()
        .context("starting server")
        .expect("start server");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_server_replies() -> Result<(), Error> {
        let server = OmahaServer::new_with_metadata(vec![
            ResponseAndMetadata {
                response: OmahaResponse::NoUpdate,
                app_id: "integration-test-appid-1".to_string(),
                version: "0.0.0.1".to_string(),
                ..Default::default()
            },
            ResponseAndMetadata {
                response: OmahaResponse::NoUpdate,
                app_id: "integration-test-appid-2".to_string(),
                version: "0.0.0.2".to_string(),
                ..Default::default()
            },
        ])
        .start()
        .context("starting server")?;

        let client = fuchsia_hyper::new_client();
        let body = json!({
            "request": {
                "app": [
                    {
                        "appid": "integration-test-appid-1",
                        "version": "0.0.0.1",
                        "updatecheck": { "updatedisabled": false }
                    },
                    {
                        "appid": "integration-test-appid-2",
                        "version": "0.0.0.2",
                        "updatecheck": { "updatedisabled": false }
                    },
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
        assert_eq!(apps.len(), 2);
        for app in apps {
            let status = app.get("updatecheck").unwrap().get("status").unwrap();
            assert_eq!(status, "noupdate");
        }
        Ok(())
    }
}
