// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The http module contains utilities for making HTTP requests.

use crate::error::{ResultExt, TokenProviderError};
use async_trait::async_trait;
use fidl_fuchsia_identity_external::Error as ApiError;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::io::AsyncReadExt;
use hyper::StatusCode;

type TokenProviderResult<T> = Result<T, TokenProviderError>;

/// Representation of an HTTP request.
pub struct HttpRequest(fidl_fuchsia_net_http::Request);

/// A builder for `HttpRequest`.
pub struct HttpRequestBuilder<'a> {
    url: String,
    method: String,
    headers: Option<Vec<fidl_fuchsia_net_http::Header>>,
    body: Option<&'a str>,
}

#[must_use = "HttpRequestBuilder must be consumed with finish()."]
impl<'a> HttpRequestBuilder<'a> {
    /// Create a new `HttpRequestBuilder`.
    pub fn new<T, U>(url: T, method: U) -> Self
    where
        String: From<T> + From<U>,
    {
        HttpRequestBuilder {
            url: String::from(url),
            method: String::from(method),
            headers: None,
            body: None,
        }
    }

    /// Add a header to the HTTP request.
    pub fn with_header<T, U>(mut self, name: T, value: U) -> Self
    where
        Vec<u8>: From<T> + From<U>,
    {
        let headers = self.headers.get_or_insert(vec![]);
        headers.push(fidl_fuchsia_net_http::Header { name: name.into(), value: value.into() });
        self
    }

    /// Adds a body to the HTTP request.  Replaces any existing body.
    pub fn set_body<T>(mut self, body: &'a T) -> Self
    where
        T: AsRef<str>,
    {
        self.body.replace(body.as_ref());
        self
    }

    /// Build an HttpRequest.
    pub fn finish(self) -> TokenProviderResult<HttpRequest> {
        let url_body = match self.body {
            Some(body_str) => {
                let vmo = zx::Vmo::create(body_str.as_bytes().len() as u64)
                    .token_provider_error(ApiError::Unknown)?;
                vmo.write(&body_str.as_bytes(), 0).token_provider_error(ApiError::Unknown)?;
                Some(fidl_fuchsia_net_http::Body::Buffer(fidl_fuchsia_mem::Buffer {
                    vmo,
                    size: body_str.as_bytes().len() as u64,
                }))
            }
            None => None,
        };

        Ok(HttpRequest(fidl_fuchsia_net_http::Request {
            url: Some(self.url),
            method: Some(self.method),
            headers: self.headers,
            body: url_body,
            deadline: None,
            ..fidl_fuchsia_net_http::Request::EMPTY
        }))
    }
}

/// Number of bytes initially allocated for retrieving an HTTP response.
const RESPONSE_BUFFER_SIZE: usize = 2048;

/// A trait expressing functionality for making requests over HTTP.
#[async_trait]
pub trait HttpClient {
    /// Asynchronously make an HTTP request.  Returns the response body if any
    /// and HTTP status code.
    async fn request(
        &self,
        http_request: HttpRequest,
    ) -> TokenProviderResult<(Option<String>, StatusCode)>;
}

/// A client capable of making HTTP requests using the Fuchsia oldhttp URL
/// Loader service.
#[derive(Clone)]
pub struct UrlLoaderHttpClient {
    url_loader: fidl_fuchsia_net_http::LoaderProxy,
}

impl UrlLoaderHttpClient {
    /// Create a new `UrlLoaderHttpClient`.
    pub fn new(url_loader: fidl_fuchsia_net_http::LoaderProxy) -> Self {
        UrlLoaderHttpClient { url_loader }
    }
}

#[async_trait]
impl HttpClient for UrlLoaderHttpClient {
    /// Asynchronously send an HTTP request using oldhttp URLLoader service.
    async fn request(
        &self,
        HttpRequest(http_request): HttpRequest,
    ) -> TokenProviderResult<(Option<String>, StatusCode)> {
        let fidl_fuchsia_net_http::Response { error, body, status_code, .. } =
            self.url_loader.fetch(http_request).await.token_provider_error(ApiError::Unknown)?;
        if error.is_some() {
            return Err(TokenProviderError::new(ApiError::Network));
        }

        let status_code =
            status_code.ok_or(TokenProviderError { api_error: ApiError::Unknown, cause: None })?;
        let status =
            StatusCode::from_u16(status_code as u16).token_provider_error(ApiError::Server)?;

        match body {
            Some(sock) => {
                let mut socket =
                    fasync::Socket::from_socket(sock).token_provider_error(ApiError::Unknown)?;
                let mut response_body = Vec::<u8>::with_capacity(RESPONSE_BUFFER_SIZE);
                socket
                    .read_to_end(&mut response_body)
                    .await
                    .token_provider_error(ApiError::Unknown)?;
                let response_str =
                    String::from_utf8(response_body).token_provider_error(ApiError::Unknown)?;
                Ok((Some(response_str), status))
            }
            None => Ok((None, status)),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Error;
    use fidl::endpoints::create_proxy_and_stream;
    use futures::prelude::*;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref TEST_URL: String = String::from("https://test/test");
    }

    #[test]
    fn test_build_http_request_empty() {
        assert_build_http_request_empty_with_method("GET");
        assert_build_http_request_empty_with_method("POST");
    }

    fn assert_build_http_request_empty_with_method(method: &str) {
        let request = HttpRequestBuilder::new(TEST_URL.as_str(), method)
            .finish()
            .expect("Failed to build HTTP request.");
        assert_eq!(
            request.0,
            fidl_fuchsia_net_http::Request {
                url: Some(TEST_URL.clone()),
                method: Some(method.to_string()),
                headers: None,
                body: None,
                deadline: None,
                ..fidl_fuchsia_net_http::Request::EMPTY
            }
        );
    }

    #[test]
    fn test_build_http_request_with_headers() {
        let request = HttpRequestBuilder::new(TEST_URL.as_str(), "GET")
            .with_header("name-1", "value-1")
            .with_header(String::from("name-2"), String::from("value-2"))
            .finish()
            .expect("Failed to build HTTP request.");
        assert_eq!(
            request.0.headers.unwrap(),
            vec![
                fidl_fuchsia_net_http::Header { name: "name-1".into(), value: "value-1".into() },
                fidl_fuchsia_net_http::Header { name: "name-2".into(), value: "value-2".into() },
            ]
        );
        assert!(request.0.body.is_none());
    }

    #[test]
    fn test_build_http_request_with_body() {
        let test_body: &str = "test-body";
        let request = HttpRequestBuilder::new(TEST_URL.as_str(), "GET")
            .set_body(&test_body)
            .finish()
            .expect("Failed to build HTTP request.");
        assert!(request.0.headers.is_none());
        match request.0.body.unwrap() {
            fidl_fuchsia_net_http::Body::Stream(_) => panic!("Expected body to be a buffer."),
            fidl_fuchsia_net_http::Body::Buffer(fidl_fuchsia_mem::Buffer { vmo, size }) => {
                assert_eq!(size as usize, test_body.len());
                let mut result_body = vec![0u8; test_body.len()];
                vmo.read(&mut result_body, 0).expect("Failed to read vmo.");
                assert_eq!(String::from_utf8(result_body).unwrap(), test_body);
            }
        }
    }

    fn url_loader_with_response(
        body: &str,
        status_code: u16,
        error: Option<fidl_fuchsia_net_http::Error>,
    ) -> fidl_fuchsia_net_http::LoaderProxy {
        let response = fidl_fuchsia_net_http::Response {
            error,
            body: Some(socket_with_body(body)),
            final_url: None,
            status_code: Some(status_code as u32),
            status_line: None,
            headers: None,
            redirect: None,
            ..fidl_fuchsia_net_http::Response::EMPTY
        };
        let (url_loader_proxy, mut url_loader_stream) =
            create_proxy_and_stream::<fidl_fuchsia_net_http::LoaderMarker>()
                .expect("Failed to create URL loader proxy.");
        fasync::Task::spawn(async move {
            let req =
                url_loader_stream.try_next().await.expect("Failed to get request from stream");
            if let Some(fidl_fuchsia_net_http::LoaderRequest::Fetch { responder, .. }) = req {
                responder.send(response).expect("Failed to send response");
            } else {
                panic!("Got unexpected URL Loader request.")
            }
        })
        .detach();
        url_loader_proxy
    }

    fn socket_with_body(body: &str) -> zx::Socket {
        let (sock_read, sock_write) =
            zx::Socket::create(zx::SocketOpts::empty()).expect("Failed to create sockets");
        sock_write.write(body.as_bytes()).expect("Failed to write to socket");
        sock_read
    }

    #[fasync::run_until_stalled(test)]
    async fn test_request() -> Result<(), Error> {
        let http_client = UrlLoaderHttpClient::new(url_loader_with_response(
            "response-body",
            StatusCode::FORBIDDEN.as_u16(),
            None,
        ));
        let request = HttpRequestBuilder::new(TEST_URL.as_str(), "GET").finish()?;
        let (response_body, status) = http_client.request(request).await?;
        assert_eq!(response_body, Some("response-body".to_string()));
        assert_eq!(status, StatusCode::FORBIDDEN);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_request_large_response() -> Result<(), Error> {
        // Verify that responses won't get truncated if larger than initial buffer
        // as Oauth tokens are of indeterminate length
        let long_body = "a".repeat(RESPONSE_BUFFER_SIZE * 2);
        let http_client = UrlLoaderHttpClient::new(url_loader_with_response(
            long_body.as_str(),
            StatusCode::OK.as_u16(),
            None,
        ));
        let request = HttpRequestBuilder::new(TEST_URL.as_str(), "GET").finish()?;
        let (response_body, status) = http_client.request(request).await?;
        assert_eq!(Some(long_body), response_body);
        assert_eq!(StatusCode::OK, status);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_network_error() -> Result<(), Error> {
        let http_client = UrlLoaderHttpClient::new(url_loader_with_response(
            "",
            0,
            Some(fidl_fuchsia_net_http::Error::Connect),
        ));
        let request = HttpRequestBuilder::new(TEST_URL.as_str(), "GET").finish()?;
        let result = http_client.request(request).await;
        assert_eq!(result.unwrap_err().api_error, ApiError::Network);
        Ok(())
    }
}

#[cfg(test)]
pub mod mock {
    use super::*;
    use std::collections::VecDeque;
    use std::sync::Mutex;

    /// A mock implementation of `HttpClient` that returns responses supplied at creation
    /// time.
    pub struct TestHttpClient {
        /// Response returned on `request`.
        responses: Mutex<VecDeque<TokenProviderResult<(Option<String>, StatusCode)>>>,
    }

    impl TestHttpClient {
        /// Create a new test client that returns the given responses during calls
        /// to `request`.
        pub fn with_responses(
            responses: Vec<TokenProviderResult<(Option<String>, StatusCode)>>,
        ) -> Self {
            TestHttpClient { responses: Mutex::new(VecDeque::from(responses)) }
        }

        /// Create a new test client that returns the given response on `request`.
        pub fn with_response(body: Option<&str>, status: StatusCode) -> Self {
            Self::with_responses(vec![Ok((body.map(str::to_string), status))])
        }

        /// Create a new test client that returns the given response on `request`.
        pub fn with_error(error: ApiError) -> Self {
            Self::with_responses(vec![Err(TokenProviderError::new(error))])
        }
    }

    #[async_trait]
    impl HttpClient for TestHttpClient {
        async fn request(
            &self,
            _http_request: HttpRequest,
        ) -> TokenProviderResult<(Option<String>, StatusCode)> {
            self.responses
                .lock()
                .unwrap()
                .pop_front()
                .expect("Mock received more requests than the supplied requests!")
        }
    }

    mod test {
        use super::*;

        fn get_http_request() -> HttpRequest {
            HttpRequestBuilder::new("http://url", "GET").finish().unwrap()
        }

        #[fasync::run_until_stalled(test)]
        async fn test_mock_with_responses() {
            let responses =
                vec![Ok((None, StatusCode::OK)), Err(TokenProviderError::new(ApiError::Unknown))];
            let test_client = TestHttpClient::with_responses(responses);

            assert_eq!(
                (None, StatusCode::OK),
                test_client.request(get_http_request()).await.unwrap()
            );

            assert_eq!(
                ApiError::Unknown,
                test_client.request(get_http_request()).await.unwrap_err().api_error
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn test_mock_with_response() {
            let test_client =
                TestHttpClient::with_response(Some("response"), StatusCode::UNAUTHORIZED);

            assert_eq!(
                (Some("response".to_string()), StatusCode::UNAUTHORIZED),
                test_client.request(get_http_request()).await.unwrap()
            );
        }

        #[fasync::run_until_stalled(test)]
        async fn test_mock_with_error() {
            let test_client = TestHttpClient::with_error(ApiError::Internal);
            assert_eq!(
                ApiError::Internal,
                test_client.request(get_http_request()).await.unwrap_err().api_error
            );
        }
    }
}
