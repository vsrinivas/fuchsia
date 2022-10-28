// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    anyhow::Error,
    derive_builder::Builder,
    fuchsia_merkle::Hash,
    hyper::{body::Bytes, header, Body, Method, Request, Response, StatusCode},
    omaha_client::cup_ecdsa::{
        test_support::{make_default_private_key_for_test, make_default_public_key_id_for_test},
        PublicKeyId,
    },
    parking_lot::Mutex,
    serde::Deserialize,
    serde_json::json,
    sha2::{Digest, Sha256},
    std::{collections::HashMap, str::FromStr},
    url::Url,
};

#[cfg(target_os = "fuchsia")]
use {
    fuchsia_async as fasync,
    futures::prelude::*,
    hyper::{
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
    },
    std::{
        convert::Infallible,
        net::{Ipv4Addr, SocketAddr},
        sync::Arc,
    },
};

#[derive(Copy, Clone, Debug, PartialEq, Eq, Deserialize)]
pub enum OmahaResponse {
    NoUpdate,
    Update,
    InvalidResponse,
    InvalidURL,
}

#[derive(Clone, Debug, Deserialize)]
pub struct ResponseAndMetadata {
    // Note: Keep this struct up-to-date with responseAndMetadata within
    // omaha_tool/omaha.go.
    pub response: OmahaResponse,
    pub merkle: Hash,
    pub check_assertion: UpdateCheckAssertion,
    pub version: Option<String>,
    pub cohort_assertion: Option<String>,
    pub codebase: String,
    pub package_path: String,
}

impl Default for ResponseAndMetadata {
    fn default() -> ResponseAndMetadata {
        ResponseAndMetadata {
            response: OmahaResponse::NoUpdate,
            merkle: Hash::from_str(
                "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
            )
            .unwrap(),
            check_assertion: UpdateCheckAssertion::UpdatesEnabled,
            version: Some("0.1.2.3".to_string()),
            cohort_assertion: None,
            codebase: "fuchsia-pkg://integration.test.fuchsia.com/".to_string(),
            package_path: "update".to_string(),
        }
    }
}

// The corresponding private key to lib/omaha-client's PublicKey. For testing
// only, since omaha-client never needs to hold a private key.
pub type PrivateKey = p256::ecdsa::SigningKey;

#[derive(Clone, Debug)]
pub struct PrivateKeyAndId {
    pub id: PublicKeyId,
    pub key: PrivateKey,
}

#[derive(Clone, Debug)]
pub struct PrivateKeys {
    pub latest: PrivateKeyAndId,
    pub historical: Vec<PrivateKeyAndId>,
}

impl PrivateKeys {
    pub fn find(&self, id: PublicKeyId) -> Option<&PrivateKey> {
        if self.latest.id == id {
            return Some(&self.latest.key);
        }
        for pair in &self.historical {
            if pair.id == id {
                return Some(&pair.key);
            }
        }
        None
    }
}

pub fn make_default_private_keys_for_test() -> PrivateKeys {
    PrivateKeys {
        latest: PrivateKeyAndId {
            id: make_default_public_key_id_for_test(),
            key: make_default_private_key_for_test(),
        },
        historical: vec![],
    }
}

pub type ResponseMap = HashMap<String, ResponseAndMetadata>;

#[derive(Copy, Clone, Debug, Deserialize)]
pub enum UpdateCheckAssertion {
    UpdatesEnabled,
    UpdatesDisabled,
}

#[derive(Clone, Debug, Builder)]
#[builder(pattern = "owned")]
#[builder(derive(Debug))]
pub struct OmahaServer {
    #[builder(default, setter(into))]
    pub responses_by_appid: ResponseMap,
    #[builder(default = "make_default_private_keys_for_test()")]
    pub private_keys: PrivateKeys,
    #[builder(default = "None")]
    pub etag_override: Option<String>,
    #[builder(default)]
    pub require_cup: bool,
}

impl OmahaServer {
    /// Sets the special assertion to make on any future update check requests
    pub fn set_all_update_check_assertions(&mut self, value: UpdateCheckAssertion) {
        for response_and_metadata in self.responses_by_appid.values_mut() {
            response_and_metadata.check_assertion = value;
        }
    }

    /// Sets the special assertion to make on any future cohort in requests
    pub fn set_all_cohort_assertions(&mut self, value: Option<String>) {
        for response_and_metadata in self.responses_by_appid.values_mut() {
            response_and_metadata.cohort_assertion = value.clone();
        }
    }

    /// Spawn the server on the current executor, returning the address of the server.
    #[cfg(target_os = "fuchsia")]
    pub fn start(arc_server: Arc<Mutex<OmahaServer>>) -> Result<String, Error> {
        let addr = SocketAddr::new(Ipv4Addr::LOCALHOST.into(), 0);

        let (connections, addr) = {
            let listener = fasync::net::TcpListener::bind(&addr)?;
            let local_addr = listener.local_addr()?;
            (
                listener
                    .accept_stream()
                    .map_ok(|(conn, _addr)| fuchsia_hyper::TcpStream { stream: conn }),
                local_addr,
            )
        };

        let make_svc = make_service_fn(move |_socket| {
            let arc_server = Arc::clone(&arc_server);
            async move {
                Ok::<_, Infallible>(service_fn(move |req| {
                    let arc_server = Arc::clone(&arc_server);
                    async move { handle_request(req, &arc_server).await }
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

fn make_etag(
    request_body: &Bytes,
    uri: &str,
    private_keys: &PrivateKeys,
    response_data: &[u8],
) -> Option<String> {
    use p256::ecdsa::signature::Signer;

    if uri == "/" {
        return None;
    }

    let parsed_uri = Url::parse(&format!("https://example.com{}", uri)).unwrap();
    let mut query_pairs = parsed_uri.query_pairs();

    let (cup2key_key, cup2key_val) = query_pairs.next().unwrap();
    assert_eq!(cup2key_key, "cup2key");

    let (public_key_id_str, _nonce_str) = cup2key_val.split_once(':').unwrap();
    let public_key_id: PublicKeyId = public_key_id_str.parse().unwrap();
    let private_key: &PrivateKey = match private_keys.find(public_key_id) {
        Some(pk) => Some(pk),
        None => {
            tracing::error!(
                "Could not find public_key_id {:?} in the private_keys map, which only knows about the latest key_id {:?} and the historical key_ids {:?}",
                public_key_id,
                private_keys.latest.id,
                private_keys.historical.iter().map(|pkid| pkid.id).collect::<Vec<_>>(),
                );
            None
        }
    }?;

    let request_hash = Sha256::digest(request_body);
    let response_hash = Sha256::digest(response_data);

    let mut hasher = Sha256::new();
    hasher.update(request_hash);
    hasher.update(response_hash);
    hasher.update(&*cup2key_val);
    let transaction_hash = hasher.finalize();

    Some(format!(
        "{}:{}",
        hex::encode(private_key.sign(&transaction_hash).to_der()),
        hex::encode(request_hash)
    ))
}

pub async fn handle_request(
    req: Request<Body>,
    omaha_server: &Mutex<OmahaServer>,
) -> Result<Response<Body>, Error> {
    if req.uri().path() == "/set_responses_by_appid" {
        return handle_set_responses(req, omaha_server).await;
    }

    return handle_omaha_request(req, omaha_server).await;
}

pub async fn handle_set_responses(
    req: Request<Body>,
    omaha_server: &Mutex<OmahaServer>,
) -> Result<Response<Body>, Error> {
    assert_eq!(req.method(), Method::POST);

    let req_body = hyper::body::to_bytes(req).await?;
    let req_json: HashMap<String, ResponseAndMetadata> =
        serde_json::from_slice(&req_body).expect("parse json");
    {
        let mut omaha_server = omaha_server.lock();
        omaha_server.responses_by_appid = req_json;
    }

    let builder = Response::builder().status(StatusCode::OK).header(header::CONTENT_LENGTH, 0);
    Ok(builder.body(Body::empty()).unwrap())
}

pub async fn handle_omaha_request(
    req: Request<Body>,
    omaha_server: &Mutex<OmahaServer>,
) -> Result<Response<Body>, Error> {
    let omaha_server = omaha_server.lock().clone();
    assert_eq!(req.method(), Method::POST);

    if omaha_server.responses_by_appid.is_empty() {
        let builder = Response::builder()
            .status(StatusCode::INTERNAL_SERVER_ERROR)
            .header(header::CONTENT_LENGTH, 0);
        tracing::error!("Received a request before |responses_by_appid| was set; returning an empty response with status 500.");
        return Ok(builder.body(Body::empty()).unwrap());
    }

    let uri_string = req.uri().to_string();

    let req_body = hyper::body::to_bytes(req).await?;
    let req_json: serde_json::Value = serde_json::from_slice(&req_body).expect("parse json");

    let request = req_json.get("request").unwrap();
    let apps = request.get("app").unwrap().as_array().unwrap();

    // If this request contains updatecheck, make sure the mock has the right number of configured apps.
    match apps.iter().filter(|app| app.get("updatecheck").is_some()).count() {
        0 => {}
        x => assert_eq!(x, omaha_server.responses_by_appid.len()),
    }

    let apps: Vec<serde_json::Value> = apps
        .iter()
        .map(|app| {
            let appid = app.get("appid").unwrap();
            let expected = &omaha_server.responses_by_appid[appid.as_str().unwrap()];

            if let Some(expected_version) = &expected.version {
                let version = app.get("version").unwrap();
                assert_eq!(version, expected_version);
            }

            let package_name = format!("{}?hash={}", expected.package_path, expected.merkle);
            let app = if let Some(expected_update_check) = app.get("updatecheck") {
                let updatedisabled = expected_update_check
                    .get("updatedisabled")
                    .map(|v| v.as_bool().unwrap())
                    .unwrap_or(false);
                match expected.check_assertion {
                    UpdateCheckAssertion::UpdatesEnabled => {
                        assert!(!updatedisabled);
                    }
                    UpdateCheckAssertion::UpdatesDisabled => {
                        assert!(updatedisabled);
                    }
                }

                if let Some(cohort_assertion) = &expected.cohort_assertion {
                    assert_eq!(
                        app.get("cohort")
                            .expect("expected cohort")
                            .as_str()
                            .expect("cohort is string"),
                        cohort_assertion
                    );
                }

                let updatecheck = match expected.response {
                    OmahaResponse::Update => json!({
                        "status": "ok",
                        "urls": {
                            "url": [
                                {
                                    "codebase": expected.codebase,
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

    let response_data: Vec<u8> = serde_json::to_vec(&response).unwrap();

    let mut builder = Response::builder()
        .status(StatusCode::OK)
        .header(header::CONTENT_LENGTH, response_data.len());

    // It is only possible to calculate an induced etag if the incoming request
    // had a valid cup2key query argument.
    let induced_etag: Option<String> =
        make_etag(&req_body, &uri_string, &omaha_server.private_keys, &response_data);

    if omaha_server.require_cup && induced_etag.is_none() {
        panic!(
            "mock-omaha-server was configured to expect CUP, but we received a request without it."
        );
    }

    if let Some(etag) = omaha_server.etag_override.as_ref().or(induced_etag.as_ref()) {
        builder = builder.header(header::ETAG, etag);
    }

    Ok(builder.body(Body::from(response_data)).unwrap())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Context,
        fuchsia_async as fasync,
        hyper::{Body, StatusCode},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_no_validate_version() -> Result<(), Error> {
        // Send a request with no specified version and assert that we don't check.
        // See 0.0.0.1 vs 9.9.9.9 below.
        let server = OmahaServer::start(Arc::new(Mutex::new(
            OmahaServerBuilder::default()
                .responses_by_appid([(
                    "integration-test-appid-1".to_string(),
                    ResponseAndMetadata {
                        response: OmahaResponse::NoUpdate,
                        version: None,
                        ..Default::default()
                    },
                )])
                .build()
                .unwrap(),
        )))
        .context("starting server")?;

        let client = fuchsia_hyper::new_client();
        let body = json!({
            "request": {
                "app": [
                    {
                        "appid": "integration-test-appid-1",
                        "version": "9.9.9.9",
                        "updatecheck": { "updatedisabled": false }
                    },
                ]
            }
        });
        let request = Request::post(&server).body(Body::from(body.to_string())).unwrap();

        let response = client.request(request).await?;

        assert_eq!(response.status(), StatusCode::OK);
        let body = hyper::body::to_bytes(response).await.context("reading response body")?;
        let obj: serde_json::Value =
            serde_json::from_slice(&body).context("parsing response json")?;

        let response = obj.get("response").unwrap();
        let apps = response.get("app").unwrap().as_array().unwrap();
        assert_eq!(apps.len(), 1);
        let status = apps[0].get("updatecheck").unwrap().get("status").unwrap();
        assert_eq!(status, "noupdate");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_server_replies() -> Result<(), Error> {
        let server_url = OmahaServer::start(Arc::new(Mutex::new(
            OmahaServerBuilder::default()
                .responses_by_appid([
                    (
                        "integration-test-appid-1".to_string(),
                        ResponseAndMetadata {
                            response: OmahaResponse::NoUpdate,
                            version: Some("0.0.0.1".to_string()),
                            ..Default::default()
                        },
                    ),
                    (
                        "integration-test-appid-2".to_string(),
                        ResponseAndMetadata {
                            response: OmahaResponse::NoUpdate,
                            version: Some("0.0.0.2".to_string()),
                            ..Default::default()
                        },
                    ),
                ])
                .build()
                .unwrap(),
        )))
        .context("starting server")?;

        {
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
            let request = Request::post(&server_url).body(Body::from(body.to_string())).unwrap();

            let response = client.request(request).await?;

            assert_eq!(response.status(), StatusCode::OK);
            let body = hyper::body::to_bytes(response).await.context("reading response body")?;
            let obj: serde_json::Value =
                serde_json::from_slice(&body).context("parsing response json")?;

            let response = obj.get("response").unwrap();
            let apps = response.get("app").unwrap().as_array().unwrap();
            assert_eq!(apps.len(), 2);
            for app in apps {
                let status = app.get("updatecheck").unwrap().get("status").unwrap();
                assert_eq!(status, "noupdate");
            }
        }

        {
            // change the expected responses; now we only configure one app,
            // 'integration-test-appid-1', which will respond with an update.
            let body = json!({
                "integration-test-appid-1": {
                    "response": "Update",
                    "merkle": "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef",
                    "check_assertion": "UpdatesEnabled",
                    "version": "0.0.0.1",
                    "codebase": "fuchsia-pkg://integration.test.fuchsia.com/",
                    "package_path": "update",
                }
            });
            let request = Request::post(format!("{server_url}set_responses_by_appid"))
                .body(Body::from(body.to_string()))
                .unwrap();
            let client = fuchsia_hyper::new_client();
            let response = client.request(request).await?;
            assert_eq!(response.status(), StatusCode::OK);
        }

        {
            let body = json!({
                "request": {
                    "app": [
                        {
                            "appid": "integration-test-appid-1",
                            "version": "0.0.0.1",
                            "updatecheck": { "updatedisabled": false }
                        },
                    ]
                }
            });
            let request = Request::post(&server_url).body(Body::from(body.to_string())).unwrap();

            let client = fuchsia_hyper::new_client();
            let response = client.request(request).await?;

            assert_eq!(response.status(), StatusCode::OK);
            let body = hyper::body::to_bytes(response).await.context("reading response body")?;
            let obj: serde_json::Value =
                serde_json::from_slice(&body).context("parsing response json")?;

            let response = obj.get("response").unwrap();
            let apps = response.get("app").unwrap().as_array().unwrap();
            assert_eq!(apps.len(), 1);
            for app in apps {
                let status = app.get("updatecheck").unwrap().get("status").unwrap();
                // We configured 'integration-test-appid-1' to respond with an update.
                assert_eq!(status, "ok");
            }
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_configured_responses() -> Result<(), Error> {
        let server = OmahaServer::start(Arc::new(Mutex::new(
            OmahaServerBuilder::default().responses_by_appid([]).build().unwrap(),
        )))
        .context("starting server")?;

        let client = fuchsia_hyper::new_client();
        let body = json!({
            "request": {
                "app": [
                    {
                        "appid": "integration-test-appid-1",
                        "version": "0.1.2.3",
                        "updatecheck": { "updatedisabled": false }
                    },
                ]
            }
        });
        let request = Request::post(&server).body(Body::from(body.to_string())).unwrap();
        let response = client.request(request).await?;
        assert_eq!(response.status(), StatusCode::INTERNAL_SERVER_ERROR);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_server_expect_cup_nopanic() -> Result<(), Error> {
        let server_url = OmahaServer::start(Arc::new(Mutex::new(
            OmahaServerBuilder::default()
                .responses_by_appid([(
                    "integration-test-appid-1".to_string(),
                    ResponseAndMetadata {
                        response: OmahaResponse::NoUpdate,
                        version: Some("0.0.0.1".to_string()),
                        ..Default::default()
                    },
                )])
                .require_cup(true)
                .build()
                .unwrap(),
        )))
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
                ]
            }
        });
        // CUP attached.
        let request = Request::post(format!(
            "{}?cup2key={}:nonce",
            &server_url,
            make_default_public_key_id_for_test()
        ))
        .body(Body::from(body.to_string()))
        .unwrap();

        let response = client.request(request).await?;

        assert_eq!(response.status(), StatusCode::OK);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic(expected = "configured to expect CUP")]
    async fn test_server_expect_cup_panic() {
        let server_url = OmahaServer::start(Arc::new(Mutex::new(
            OmahaServerBuilder::default()
                .responses_by_appid([(
                    "integration-test-appid-1".to_string(),
                    ResponseAndMetadata {
                        response: OmahaResponse::NoUpdate,
                        version: Some("0.0.0.1".to_string()),
                        ..Default::default()
                    },
                )])
                .require_cup(true)
                .build()
                .unwrap(),
        )))
        .context("starting server")
        .unwrap();

        let client = fuchsia_hyper::new_client();
        let body = json!({
            "request": {
                "app": [
                    {
                        "appid": "integration-test-appid-1",
                        "version": "0.0.0.1",
                        "updatecheck": { "updatedisabled": false }
                    },
                ]
            }
        });
        // no CUP, but we set .require_cup(true) above, so mock-omaha-server will
        // panic. (See should_panic above.)
        let request = Request::post(&server_url).body(Body::from(body.to_string())).unwrap();
        let _response = client.request(request).await.unwrap();
    }
}
