// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::rest::visualizer::*,
    anyhow::Error,
    log::{info, warn},
    rouille::{Request, Response, ResponseBody},
    scrutiny::{
        engine::dispatcher::{ControllerDispatcher, DispatcherError},
        model::controller::ConnectionMode,
    },
    serde_json::json,
    std::collections::HashMap,
    std::io::{self, ErrorKind, Read},
    std::str,
    std::sync::{Arc, RwLock},
    std::thread,
};

/// Holds ownership of the thread that the REST service is running on.
pub struct RestService {}

impl RestService {
    /// Spawns the RestService on a new thread.
    pub fn spawn(
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
        visualizer: Arc<RwLock<Visualizer>>,
        port: u16,
    ) {
        let addr = format!("127.0.0.1:{}", port);
        println!("• Server: http://{}\n", addr);
        thread::spawn(move || RestService::run(dispatcher, visualizer, addr));
    }

    /// Runs the core REST service loop, parsing URLs and queries to their
    /// respective controllers via the ControllerDispatcher. This function does
    /// not exit.
    fn run(
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
        visualizer: Arc<RwLock<Visualizer>>,
        addr: String,
    ) {
        info!("Server starting: http://{}", addr);
        rouille::start_server(addr, move |request| {
            info!("Request: {} {}", request.method(), request.url());
            // TODO: Change to allow each plugin to define its own visualizers.
            if request.url().starts_with("/api") {
                RestService::handle_controller_request(dispatcher.clone(), request)
            } else {
                visualizer.read().unwrap().serve_path_or_index(request)
            }
        });
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

    fn handle_controller_request(
        dispatcher: Arc<RwLock<ControllerDispatcher>>,
        request: &Request,
    ) -> Response {
        let method = request.method();
        let mut body = request.data().expect("RequestBody already retrieved");

        let query_val = match method {
            "GET" => {
                // TODO: Looking at the source for `get_param(&self, param_name: &str)` seems like it's not great to
                // rely on that function since it doesn't match against the entire parameter name...
                let query = request.raw_query_string();
                let params = RestService::parse_get_params(query);
                Ok(json!(params))
            }
            "POST" => {
                let mut query = String::new();
                if let Err(e) = body.read_to_string(&mut query) {
                    warn!("Failed to read request body.");
                    return RestService::error_response(Error::new(e), Some(400));
                }
                if query.is_empty() {
                    // If there is no body, return a null value, since from_str will error.
                    Ok(json!(null))
                } else {
                    serde_json::from_str(&query)
                }
            }
            _ => {
                // TODO: Should always serve HEAD requests.
                warn!("Expected GET or POST method, received {}.", method);
                return RestService::error_response(
                    Error::new(io::Error::new(ErrorKind::ConnectionRefused, "Unsupported method.")),
                    Some(405),
                );
            }
        };

        let dispatch = dispatcher.read().unwrap();
        if let Ok(json_val) = query_val {
            match dispatch.query(ConnectionMode::Remote, request.url(), json_val) {
                Ok(result) => Response {
                    status_code: 200,
                    headers: vec![("Content-Type".into(), "application/json".into())],
                    data: ResponseBody::from_string(serde_json::to_string_pretty(&result).unwrap()),
                    upgrade: None,
                },
                Err(e) => {
                    if let Some(dispatch_error) = e.downcast_ref::<DispatcherError>() {
                        if let DispatcherError::NamespaceDoesNotExist(_) = dispatch_error {
                            warn!("Address not found.");
                            return Response::empty_404();
                        }
                    }
                    RestService::error_response(e, None)
                }
            }
        } else {
            return Response::empty_400();
        }
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        anyhow::Result,
        scrutiny::{model::controller::DataController, model::model::DataModel},
        serde_json::value::Value,
        std::io,
        tempfile::tempdir,
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

    fn test_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
    }

    fn setup_dispatcher() -> Arc<RwLock<ControllerDispatcher>> {
        let data_model = test_model();
        let mut dispatcher = ControllerDispatcher::new(data_model);
        let echo = Arc::new(EchoController::default());
        let error = Arc::new(ErrorController::default());
        dispatcher.add(Uuid::new_v4(), "/api/foo/bar".to_string(), echo).unwrap();
        dispatcher.add(Uuid::new_v4(), "/api/foo/baz".to_string(), error).unwrap();
        Arc::new(RwLock::new(dispatcher))
    }

    #[test]
    fn handle_controller_request_fails_non_get_or_post_request() {
        let dispatcher = setup_dispatcher();
        let request = &Request::fake_http("HEAD", "/api/foo/bar", vec![], vec![]);
        let response = RestService::handle_controller_request(dispatcher.clone(), request);
        assert_eq!(response.status_code, 405);
    }

    #[test]
    fn handle_controller_request_returns_500_on_controller_error() {
        let dispatcher = setup_dispatcher();
        let request = &Request::fake_http("GET", "/api/foo/baz", vec![], vec![]);
        let response = RestService::handle_controller_request(dispatcher.clone(), request);
        assert_eq!(response.status_code, 500);
    }

    #[test]
    fn handle_controller_request_returns_404_on_non_matching_dispatcher() {
        let dispatcher = setup_dispatcher();
        let request = &Request::fake_http("GET", "/api/foo/bin", vec![], vec![]);
        let response = RestService::handle_controller_request(dispatcher.clone(), request);
        assert_eq!(response.status_code, 404);
    }

    #[test]
    fn handle_controller_request_serves_get_request() {
        let dispatcher = setup_dispatcher();
        let request = &Request::fake_http("GET", "/api/foo/bar?hello=world", vec![], vec![]);
        let response = RestService::handle_controller_request(dispatcher.clone(), request);
        assert_eq!(response.status_code, 200);
        let mut buffer = Vec::new();
        let (mut reader, _) = response.data.into_reader_and_size();
        reader.read_to_end(&mut buffer).unwrap();
        let response_str = std::str::from_utf8(&buffer).unwrap();
        assert_eq!(response_str.contains("hello"), true);
        assert_eq!(response_str.contains("world"), true);
    }

    #[test]
    fn handle_controller_request_serves_post_request() {
        let dispatcher = setup_dispatcher();
        let bytes = b"{\"hello\":\"world\"}";
        let request = &Request::fake_http("POST", "/api/foo/bar", vec![], bytes.to_vec());
        let response = RestService::handle_controller_request(dispatcher.clone(), request);
        assert_eq!(response.status_code, 200);
        let mut buffer = Vec::new();
        let (mut reader, _) = response.data.into_reader_and_size();
        reader.read_to_end(&mut buffer).unwrap();
        let response_str = std::str::from_utf8(&buffer).unwrap();
        assert_eq!(response_str.contains("hello"), true);
        assert_eq!(response_str.contains("world"), true);
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
