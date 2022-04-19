// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async::{self as fasync, net::TcpListener},
    fuchsia_merkle::Hash,
    futures::prelude::*,
    hyper::{
        body::Bytes,
        header,
        server::{accept::from_stream, Server},
        service::{make_service_fn, service_fn},
        Body, Method, Request, Response, StatusCode,
    },
    omaha_client::cup_ecdsa::{PublicKeyId, RequestMetadata},
    parking_lot::Mutex,
    serde_json::json,
    std::{
        collections::HashMap,
        convert::Infallible,
        net::{Ipv4Addr, SocketAddr},
        str::FromStr,
        sync::Arc,
    },
    url::Url,
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
            version: "0.1.2.3".to_string(),
        }
    }
}

/// Shared state.
#[derive(Clone, Debug)]
struct Inner {
    pub responses_by_appid: HashMap<String, ResponseAndMetadata>,
    pub private_keys: Option<PrivateKeys>,
}

impl Inner {
    pub fn num_apps(&self) -> usize {
        self.responses_by_appid.len()
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
    pub fn set(
        mut self,
        responses_by_appid: impl IntoIterator<Item = (String, ResponseAndMetadata)>,
    ) -> Self {
        self.inner.responses_by_appid = responses_by_appid.into_iter().collect();
        self
    }

    pub fn private_keys(mut self, private_keys: PrivateKeys) -> Self {
        self.inner.private_keys = Some(private_keys);
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
            inner: Inner {
                responses_by_appid: HashMap::from([(
                    "integration-test-appid".to_string(),
                    ResponseAndMetadata::default(),
                )]),
                private_keys: None,
            },
        }
    }

    pub fn new(responses: Vec<(String, OmahaResponse)>) -> Self {
        Self::builder()
            .set(responses.into_iter().map(|(appid, response)| {
                (appid, ResponseAndMetadata { response, ..Default::default() })
            }))
            .build()
    }

    pub fn new_with_metadata(responses_by_appid: Vec<(String, ResponseAndMetadata)>) -> Self {
        Self::builder().set(responses_by_appid).build()
    }

    /// Sets the special assertion to make on any future update check requests
    pub fn set_all_update_check_assertions(&self, value: Option<UpdateCheckAssertion>) {
        for response_and_metadata in self.inner.lock().responses_by_appid.values_mut() {
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

fn make_etag(
    request_body: &Bytes,
    uri: &str,
    inner: &Inner,
    response_data: &[u8],
) -> Option<String> {
    use p256::ecdsa::signature::Signer;
    use std::convert::TryInto;

    if uri == "/" {
        return None;
    }

    let parsed_uri = Url::parse(&format!("https://example.com{}", uri)).unwrap();
    let mut query_pairs = parsed_uri.query_pairs();

    let (cup2key_key, cup2key_val) = query_pairs.next().unwrap();
    assert_eq!(cup2key_key, "cup2key");

    let (public_key_id_str, nonce_str) = cup2key_val.split_once(':').unwrap();
    let public_key_id: PublicKeyId = public_key_id_str.parse().unwrap();

    let request_metadata_hash = (RequestMetadata {
        request_body: request_body.to_vec().clone(),
        public_key_id,
        nonce: hex::decode(nonce_str).unwrap().try_into().unwrap(),
    })
    .hash();

    let private_key: &PrivateKey =
        inner.private_keys.as_ref().unwrap().find(public_key_id).unwrap();

    let mut buffer: Vec<u8> = vec![];
    buffer.extend(response_data.iter());
    buffer.extend(request_metadata_hash.iter());

    Some(format!(
        "{}:{}",
        hex::encode(private_key.sign(&buffer).to_der()),
        hex::encode(request_metadata_hash)
    ))
}

async fn handle_omaha_request(
    req: Request<Body>,
    inner: &Mutex<Inner>,
) -> Result<Response<Body>, Error> {
    let inner = inner.lock().clone();

    assert_eq!(req.method(), Method::POST);

    let uri_string = req.uri().to_string();

    let req_body = hyper::body::to_bytes(req).await?;
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
            let expected = &inner.responses_by_appid[appid.as_str().unwrap()];

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

    let response_data: Vec<u8> = serde_json::to_vec(&response).unwrap();

    let mut builder = Response::builder()
        .status(StatusCode::OK)
        .header(header::CONTENT_LENGTH, response_data.len());
    if let Some(etag) = make_etag(&req_body, &uri_string, &inner, &response_data) {
        builder = builder.header(header::ETAG, etag);
    }
    Ok(builder.body(Body::from(response_data)).unwrap())
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
        let server = OmahaServer::new_with_metadata(vec![
            (
                "integration-test-appid-1".to_string(),
                ResponseAndMetadata {
                    response: OmahaResponse::NoUpdate,
                    version: "0.0.0.1".to_string(),
                    ..Default::default()
                },
            ),
            (
                "integration-test-appid-2".to_string(),
                ResponseAndMetadata {
                    response: OmahaResponse::NoUpdate,
                    version: "0.0.0.2".to_string(),
                    ..Default::default()
                },
            ),
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
        Ok(())
    }
}
