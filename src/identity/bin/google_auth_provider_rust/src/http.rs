// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The http module contains utilities for making HTTP requests.

use crate::error::{AuthProviderError, ResultExt};
use fidl_fuchsia_auth::AuthProviderStatus;
use fidl_fuchsia_mem;
use fidl_fuchsia_net_oldhttp::{
    CacheMode, HttpHeader, ResponseBodyMode, UrlBody, UrlLoaderProxy, UrlRequest, UrlResponse,
};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::future::FutureObj;
use futures::io::AsyncReadExt;
use hyper::StatusCode;
use log::warn;

type AuthProviderResult<T> = Result<T, AuthProviderError>;

/// Representation of an HTTP request.
pub struct HttpRequest(UrlRequest);

/// A builder for `HttpRequest`.
pub struct HttpRequestBuilder<'a> {
    url: String,
    method: String,
    headers: Option<Vec<HttpHeader>>,
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
        String: From<T> + From<U>,
    {
        let headers = self.headers.get_or_insert(vec![]);
        headers.push(HttpHeader { name: String::from(name), value: String::from(value) });
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
    pub fn finish(self) -> AuthProviderResult<HttpRequest> {
        let url_body = match self.body {
            Some(body_str) => {
                let vmo = zx::Vmo::create(body_str.as_bytes().len() as u64)
                    .auth_provider_status(AuthProviderStatus::UnknownError)?;
                vmo.write(&body_str.as_bytes(), 0)
                    .auth_provider_status(AuthProviderStatus::UnknownError)?;
                Some(Box::new(UrlBody::Buffer(fidl_fuchsia_mem::Buffer {
                    vmo,
                    size: body_str.as_bytes().len() as u64,
                })))
            }
            None => None,
        };

        Ok(HttpRequest(UrlRequest {
            url: self.url,
            method: self.method,
            headers: self.headers,
            body: url_body,
            response_body_buffer_size: 0,
            auto_follow_redirects: false,
            cache_mode: CacheMode::BypassCache,
            response_body_mode: ResponseBodyMode::Stream,
        }))
    }
}

/// Number of bytes initially allocated for retrieving an HTTP response.
const RESPONSE_BUFFER_SIZE: usize = 2048;

/// A trait expressing functionality for making requests over HTTP.
pub trait HttpClient {
    /// Asynchronously make an HTTP request.  Returns the response body if any
    /// and HTTP status code.
    fn request<'a>(
        &'a self,
        http_request: HttpRequest,
    ) -> FutureObj<'a, AuthProviderResult<(Option<String>, StatusCode)>>;
}

/// A client capable of making HTTP requests using the Fuchsia oldhttp URL
/// Loader service.
pub struct UrlLoaderHttpClient {
    url_loader: UrlLoaderProxy,
}

impl UrlLoaderHttpClient {
    /// Create a new `UrlLoaderHttpClient`.
    pub fn new(url_loader: UrlLoaderProxy) -> Self {
        UrlLoaderHttpClient { url_loader }
    }

    async fn request_inner(
        &self,
        http_request: HttpRequest,
    ) -> AuthProviderResult<(Option<String>, StatusCode)> {
        let mut request = http_request.0;

        let UrlResponse { error, body: response_body, status_code, .. } =
            await!(self.url_loader.start(&mut request))
                .auth_provider_status(AuthProviderStatus::UnknownError)?;
        if error.is_some() {
            return Err(AuthProviderError::new(AuthProviderStatus::NetworkError));
        }

        let status = StatusCode::from_u16(status_code as u16)
            .auth_provider_status(AuthProviderStatus::OauthServerError)?;

        match response_body.map(|x| *x) {
            Some(UrlBody::Stream(sock)) => {
                let mut socket = fasync::Socket::from_socket(sock)
                    .auth_provider_status(AuthProviderStatus::UnknownError)?;
                let mut response_body = Vec::<u8>::with_capacity(RESPONSE_BUFFER_SIZE);
                await!(socket.read_to_end(&mut response_body))
                    .auth_provider_status(AuthProviderStatus::UnknownError)?;
                let response_str = String::from_utf8(response_body)
                    .auth_provider_status(AuthProviderStatus::UnknownError)?;
                Ok((Some(response_str), status))
            }
            Some(UrlBody::Buffer(_)) => {
                warn!("URL loader response unexpectedly contained a buffer instead of a stream");
                Err(AuthProviderError::new(AuthProviderStatus::UnknownError))
            }
            None => Ok((None, status)),
        }
    }
}

impl HttpClient for UrlLoaderHttpClient {
    /// Asynchronously send an HTTP request using oldhttp URLLoader service.
    fn request<'a>(
        &'a self,
        http_request: HttpRequest,
    ) -> FutureObj<'a, AuthProviderResult<(Option<String>, StatusCode)>> {
        FutureObj::new(Box::new(async move { await!(Self::request_inner(self, http_request)) }))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use failure::Error;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_net_oldhttp::{HttpError, UrlLoaderMarker, UrlLoaderRequest};
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
            UrlRequest {
                url: TEST_URL.clone(),
                method: method.to_string(),
                headers: None,
                body: None,
                response_body_buffer_size: 0,
                auto_follow_redirects: false,
                cache_mode: CacheMode::BypassCache,
                response_body_mode: ResponseBodyMode::Stream
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
                HttpHeader { name: "name-1".to_string(), value: "value-1".to_string() },
                HttpHeader { name: "name-2".to_string(), value: "value-2".to_string() },
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
        match *request.0.body.unwrap() {
            UrlBody::Stream(_) => panic!("Expected body to be a buffer."),
            UrlBody::Buffer(fidl_fuchsia_mem::Buffer { vmo, size }) => {
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
        error: Option<i32>,
    ) -> UrlLoaderProxy {
        let mut response = UrlResponse {
            error: error.map(|code| Box::new(HttpError { code: code, description: None })),
            body: Some(Box::new(UrlBody::Stream(socket_with_body(body)))),
            url: None,
            status_code: status_code as u32,
            status_line: None,
            headers: None,
            mime_type: None,
            charset: None,
            redirect_method: None,
            redirect_url: None,
            redirect_referrer: None,
        };
        let (url_loader_proxy, mut url_loader_stream) =
            create_proxy_and_stream::<UrlLoaderMarker>()
                .expect("Failed to create URL loader proxy.");
        fasync::spawn(async move {
            let req =
                await!(url_loader_stream.try_next()).expect("Failed to get request from stream");
            if let Some(UrlLoaderRequest::Start { responder, .. }) = req {
                responder.send(&mut response).expect("Failed to send response");
            } else {
                panic!("Got unexpected URL Loader request.")
            }
        });
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
        let (response_body, status) = await!(http_client.request(request))?;
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
        let (response_body, status) = await!(http_client.request(request))?;
        assert_eq!(Some(long_body), response_body);
        assert_eq!(StatusCode::OK, status);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_network_error() -> Result<(), Error> {
        let http_client = UrlLoaderHttpClient::new(url_loader_with_response("", 0, Some(0)));
        let request = HttpRequestBuilder::new(TEST_URL.as_str(), "GET").finish()?;
        let result = await!(http_client.request(request));
        assert_eq!(result.unwrap_err().status, AuthProviderStatus::NetworkError);
        Ok(())
    }
}
