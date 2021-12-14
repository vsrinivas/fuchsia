// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::rest::visualizer::*,
    anyhow::Error,
    async_net::TcpListener,
    fuchsia_async as fasync,
    fuchsia_hyper::TcpStream,
    futures::{FutureExt as _, TryStreamExt as _},
    hyper::{header, Body, Request, Response, StatusCode},
    log::{info, warn},
    scrutiny::{
        engine::dispatcher::{ControllerDispatcher, DispatcherError},
        model::controller::ConnectionMode,
    },
    serde_json::json,
    std::collections::HashMap,
    std::convert::TryInto as _,
    std::io::{self, ErrorKind},
    std::net::{IpAddr, SocketAddr},
    std::sync::{Arc, RwLock},
};

/// Holds ownership of the thread that the REST service is running on.
pub struct RestService {}

impl RestService {
    /// Spawns the RestService on a new thread.
    ///
    /// Runs the core REST service loop, parsing URLs and queries to their
    /// respective controllers via the ControllerDispatcher. This function does
    /// not exit.
    pub fn spawn(
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
        visualizer: Arc<RwLock<Visualizer>>,
        port: u16,
    ) {
        use hyper::service::{make_service_fn, service_fn};
        use std::convert::Infallible;

        let addr = SocketAddr::new(IpAddr::V4(std::net::Ipv4Addr::LOCALHOST), port);

        let make_svc = make_service_fn(move |_: &TcpStream| {
            let dispatcher = dispatcher.clone();
            let visualizer = visualizer.clone();
            async move {
                Ok::<_, Infallible>(service_fn(move |request| {
                    let dispatcher = dispatcher.clone();
                    let visualizer = visualizer.clone();
                    async move {
                        info!("Request: {} {}", request.method(), request.uri().path());
                        // TODO: Change to allow each plugin to define its own visualizers.
                        if request.uri().path().starts_with("/api") {
                            RestService::handle_controller_request(dispatcher, request).await
                        } else {
                            visualizer.read().unwrap().serve_path_or_index(request)
                        }
                    }
                    .map(Ok::<_, Infallible>)
                }))
            }
        });

        fasync::Task::spawn(async move {
            async move {
                let listener = TcpListener::bind(addr).await?;

                let addr = listener.local_addr()?;

                info!("Server starting: http://{}", addr);

                let stream = listener.incoming().map_ok(|stream| TcpStream { stream });
                let () = hyper::Server::builder(hyper::server::accept::from_stream(stream))
                    .serve(make_svc)
                    .await?;

                Ok::<(), Error>(())
            }
            .await
            .expect("http server exited")
        })
        .detach()
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
                        // TODO(arkay) Make this work for non string-string kv pairs.
                        let (key, value) = kv.split_at(ind);
                        Some((key.to_string(), value[1..].to_string()))
                    }
                } else {
                    None
                }
            })
            .flatten()
            .collect()
    }

    /// Converts a rust error into a JSON error response.
    fn error_response(error: Error, status_code: Option<StatusCode>) -> Response<Body> {
        let result = json!({
            "status": "error",
            "description": error.to_string(),
        });

        let mut response = Response::new(result.to_string().into());
        *response.status_mut() = status_code.unwrap_or(StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(
            response
                .headers_mut()
                .insert(header::CONTENT_TYPE, "application/json".try_into().unwrap()),
            None
        );
        response
    }

    async fn handle_controller_request(
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
        request: Request<Body>,
    ) -> Response<Body> {
        use bytes::{buf::ext::BufExt as _, Buf as _};
        use hyper::Method;

        let path = request.uri().path().to_string();

        let query_val = match request.method() {
            &Method::GET => {
                // TODO: Looking at the source for `get_param(&self, param_name: &str)` seems like it's not great to
                // rely on that function since it doesn't match against the entire parameter name...
                let query = match request.uri().query() {
                    Some(query) => query,
                    None => {
                        warn!("Failed to read request parameters.");
                        return RestService::error_response(
                            anyhow::anyhow!("missing request parameters"),
                            Some(StatusCode::BAD_REQUEST),
                        );
                    }
                };
                let params = RestService::parse_get_params(query);
                Ok(json!(params))
            }
            &Method::POST => {
                let query = match hyper::body::aggregate(request.into_body()).await {
                    Ok(query) => query,
                    Err(e) => {
                        warn!("Failed to read request body.");
                        return RestService::error_response(
                            Error::new(e),
                            Some(StatusCode::BAD_REQUEST),
                        );
                    }
                };
                if query.has_remaining() {
                    serde_json::from_reader(query.reader())
                } else {
                    // If there is no body, return a null value, since from_str will error.
                    Ok(json!(null))
                }
            }
            method => {
                // TODO: Should always serve HEAD requests.
                warn!("Expected GET or POST method, received {}.", method);
                return RestService::error_response(
                    Error::new(io::Error::new(ErrorKind::ConnectionRefused, "Unsupported method.")),
                    Some(StatusCode::METHOD_NOT_ALLOWED),
                );
            }
        };

        let dispatch = dispatcher.read().unwrap();
        if let Ok(json_val) = query_val {
            match dispatch.query(ConnectionMode::Remote, path, json_val) {
                Ok(result) => {
                    let mut response =
                        Response::new(serde_json::to_string_pretty(&result).unwrap().into());
                    assert_eq!(
                        response
                            .headers_mut()
                            .insert(header::CONTENT_TYPE, "application/json".try_into().unwrap()),
                        None
                    );
                    response
                }
                Err(e) => {
                    if let Some(dispatch_error) = e.downcast_ref::<DispatcherError>() {
                        if let DispatcherError::NamespaceDoesNotExist(_) = dispatch_error {
                            warn!("Address not found.");
                            let mut response = Response::new(Body::empty());
                            *response.status_mut() = StatusCode::NOT_FOUND;
                            return response;
                        }
                    }
                    RestService::error_response(e, None)
                }
            }
        } else {
            let mut response = Response::new(Body::empty());
            *response.status_mut() = StatusCode::BAD_REQUEST;
            response
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Result,
        scrutiny::{model::controller::DataController, model::model::DataModel},
        scrutiny_testing::fake::*,
        serde_json::value::Value,
        std::io,
        uuid::Uuid,
    };

    #[derive(Default)]
    struct EchoController {}

    impl DataController for EchoController {
        fn query(&self, _: Arc<DataModel>, query: Value) -> Result<Value> {
            Ok(query)
        }
    }

    #[derive(Default)]
    struct ErrorController {}

    impl DataController for ErrorController {
        fn query(&self, _: Arc<DataModel>, _: Value) -> Result<Value> {
            Err(Error::new(io::Error::new(io::ErrorKind::Other, "It's always an error!")))
        }
    }

    fn setup_dispatcher() -> Arc<RwLock<ControllerDispatcher>> {
        let data_model = fake_data_model();
        let mut dispatcher = ControllerDispatcher::new(data_model);
        let echo = Arc::new(EchoController::default());
        let error = Arc::new(ErrorController::default());
        dispatcher.add(Uuid::new_v4(), "/api/foo/bar".to_string(), echo).unwrap();
        dispatcher.add(Uuid::new_v4(), "/api/foo/baz".to_string(), error).unwrap();
        Arc::new(RwLock::new(dispatcher))
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_controller_request_fails_non_get_or_post_request() {
        let dispatcher = setup_dispatcher();
        let request =
            Request::builder().method("HEAD").uri("/api/foo/bar").body(Body::empty()).unwrap();
        let response = RestService::handle_controller_request(dispatcher.clone(), request).await;
        assert_eq!(response.status(), StatusCode::METHOD_NOT_ALLOWED);
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_controller_request_returns_500_on_controller_error() {
        let dispatcher = setup_dispatcher();
        let request = Request::builder()
            .method("GET")
            .uri("/api/foo/baz?foo=bar")
            .body(Body::empty())
            .unwrap();
        let response = RestService::handle_controller_request(dispatcher.clone(), request).await;
        assert_eq!(response.status(), StatusCode::INTERNAL_SERVER_ERROR);
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_controller_request_returns_404_on_non_matching_dispatcher() {
        let dispatcher = setup_dispatcher();
        let request = Request::builder()
            .method("GET")
            .uri("/api/foo/bin?foo=bar")
            .body(Body::empty())
            .unwrap();
        let response = RestService::handle_controller_request(dispatcher.clone(), request).await;
        assert_eq!(response.status(), StatusCode::NOT_FOUND);
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_controller_request_serves_get_request() {
        let dispatcher = setup_dispatcher();
        let request = Request::builder()
            .method("GET")
            .uri("/api/foo/bar?hello=world")
            .body(Body::empty())
            .unwrap();
        let response = RestService::handle_controller_request(dispatcher.clone(), request).await;
        assert_eq!(response.status(), 200);
        let buffer = hyper::body::to_bytes(response.into_body()).await.unwrap();
        let response_str = std::str::from_utf8(&buffer).unwrap();
        assert!(
            response_str.contains("hello") && response_str.contains("world"),
            "{}",
            response_str
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn handle_controller_request_serves_post_request() {
        let dispatcher = setup_dispatcher();
        let bytes = b"{\"hello\":\"world\"}";
        let request =
            Request::builder().method("POST").uri("/api/foo/bar").body(bytes[..].into()).unwrap();
        let response = RestService::handle_controller_request(dispatcher.clone(), request).await;
        assert_eq!(response.status(), 200);
        let buffer = hyper::body::to_bytes(response.into_body()).await.unwrap();
        let response_str = std::str::from_utf8(&buffer).unwrap();
        assert!(
            response_str.contains("hello") && response_str.contains("world"),
            "{}",
            response_str
        );
    }

    #[test]
    fn parse_get_params_returns_empty_vec_on_empty_query() {
        let params = RestService::parse_get_params("");
        assert!(params.is_empty());
    }

    #[test]
    fn parse_get_params_skips_invalid_key_value_pairs() {
        let params = RestService::parse_get_params(
            "foo=bar&\
            &\
            baz=&\
            =aries&\
            hello=world",
        );
        assert_eq!(params.len(), 2);
        assert_eq!(params.get("foo").unwrap(), "bar");
        assert_eq!(params.get("hello").unwrap(), "world");
    }
}
