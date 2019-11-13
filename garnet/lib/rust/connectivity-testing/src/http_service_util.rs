// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, Error, ResultExt},
    fidl_fuchsia_net_oldhttp::{self as http, HttpServiceProxy},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon as zx,
    futures::io::{AllowStdIo, copy},
};

pub fn create_url_request<S: ToString>(url_string: S) -> http::UrlRequest {
    http::UrlRequest {
        url: url_string.to_string(),
        method: String::from("GET"),
        headers: None,
        body: None,
        response_body_buffer_size: 0,
        auto_follow_redirects: true,
        cache_mode: http::CacheMode::Default,
        response_body_mode: http::ResponseBodyMode::Stream,
    }
}

// Object to hold results of a single download
#[derive(Default)]
pub struct IndividualDownload {
    pub bytes: u64,
    pub nanos: u64,
    pub goodput_mbps: f64,
}

// TODO (NET-1664): verify checksum on data received
pub async fn fetch_and_discard_url(
    http_service: &HttpServiceProxy,
    mut url_request: http::UrlRequest,
) -> Result<IndividualDownload, Error> {
    // Create a UrlLoader instance
    let (s, p) = zx::Channel::create().context("failed to create zx channel")?;
    let proxy = fasync::Channel::from_channel(p).context("failed to make async channel")?;

    let loader_server = fidl::endpoints::ServerEnd::<http::UrlLoaderMarker>::new(s);
    http_service.create_url_loader(loader_server)?;

    let loader_proxy = http::UrlLoaderProxy::new(proxy);
    let start_time = zx::Time::get(zx::ClockId::Monotonic);
    let response = loader_proxy.start(&mut url_request).await?;

    if let Some(e) = response.error {
        bail!("UrlLoaderProxy error - code:{} ({})", e.code, e.description.unwrap_or("".into()))
    }

    let socket = match response.body.map(|x| *x) {
        Some(http::UrlBody::Stream(s)) => fasync::Socket::from_socket(s)?,
        _ => {
            bail!("failed to read UrlBody from the stream - error: {}", zx::Status::BAD_STATE);
        }
    };

    // discard the bytes
    let mut stdio_sink = AllowStdIo::new(::std::io::sink());
    let bytes_received = copy(socket, &mut stdio_sink).await?;
    let stop_time = zx::Time::get(zx::ClockId::Monotonic);

    let time_nanos = (stop_time - start_time).into_nanos() as u64;
    let time_seconds = time_nanos as f64 * 1e-9;

    let bits_received = (bytes_received * 8) as f64;

    fx_log_info!("Received {} bytes in {:.3} seconds", bytes_received, time_seconds);

    if bytes_received < 1 {
        bail!("Failed to download data from url! bytes_received = {}", bytes_received);
    }

    let megabits_per_sec = bits_received * 1e-6 / time_seconds;

    let mut individual_download = IndividualDownload::default();
    individual_download.goodput_mbps = megabits_per_sec;
    individual_download.bytes = bytes_received;
    individual_download.nanos = time_nanos;

    Ok(individual_download)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints,
        //fidl::endpoints::RequestStream,
        fidl_fuchsia_net_oldhttp as http,
        fidl_fuchsia_net_oldhttp::HttpError,
        fidl_fuchsia_net_oldhttp::{HttpServiceMarker, HttpServiceProxy},
        fidl_fuchsia_net_oldhttp::{HttpServiceRequest, HttpServiceRequestStream},
        fidl_fuchsia_net_oldhttp::{UrlBody, UrlRequest, UrlResponse},
        fidl_fuchsia_net_oldhttp::{UrlLoaderRequest, UrlLoaderRequestStream},
        fuchsia_async as fasync,
        futures::stream::{StreamExt, StreamFuture},
        futures::task::Poll,
        pin_utils::pin_mut,
    };

    #[test]
    fn verify_basic_url_request_creation() {
        let test_url = "https://test.example/sample/url";
        let url_req = create_url_request(test_url.to_string());

        assert_eq!(url_req.url, test_url);
        assert_eq!(url_req.method, "GET".to_string());
        assert!(url_req.headers.is_none());
        assert!(url_req.body.is_none());
        assert_eq!(url_req.response_body_buffer_size, 0);
        assert!(url_req.auto_follow_redirects);
        assert_eq!(url_req.cache_mode, http::CacheMode::Default);
        assert_eq!(url_req.response_body_mode, http::ResponseBodyMode::Stream);
    }

    #[test]
    fn response_error_triggers_error_path() {
        let test_url = "https://test.example/sample/url";
        let url_req = create_url_request(test_url.to_string());

        let url_response = create_url_response(None, None, 404);

        let download_result = trigger_download_with_supplied_response(url_req, url_response);
        assert!(download_result.is_err());
    }

    #[test]
    fn successful_download_returns_valid_indvidual_download_data() {
        let test_url = "https://test.example/sample/url";
        let url_req = create_url_request(test_url.to_string());

        // creating a response with some bytes "downloaded"
        let bytes = "there are some bytes".as_bytes();
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let url_body = Some(Box::new(http::UrlBody::Stream(s2)));
        let expected_num_bytes = s1.write(bytes).expect("failed to write response body") as u64;
        drop(s1);

        let url_response = create_url_response(None, url_body, 200);

        let request_result = trigger_download_with_supplied_response(url_req, url_response);
        let download_result = request_result.expect("failed to get individual_download");
        assert_eq!(download_result.bytes, expected_num_bytes);
    }

    #[test]
    fn zero_byte_download_triggers_error() {
        let test_url = "https://test.example/sample/url";
        let url_req = create_url_request(test_url.to_string());

        // creating a response with some bytes "downloaded"
        let bytes = "".as_bytes();
        let (s1, s2) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let url_body = Some(Box::new(http::UrlBody::Stream(s2)));
        let expected_num_bytes = s1.write(bytes).expect("failed to write response body") as u64;
        drop(s1);
        assert_eq!(expected_num_bytes, 0);

        let url_response = create_url_response(None, url_body, 200);

        let download_result = trigger_download_with_supplied_response(url_req, url_response);
        assert!(download_result.is_err());
    }

    #[test]
    fn null_response_body_triggers_error() {
        let test_url = "https://test.example/sample/url";
        let url_req = create_url_request(test_url.to_string());

        // creating a response with 0 bytes downloaded
        let url_response = create_url_response(None, None, 200);

        let download_result = trigger_download_with_supplied_response(url_req, url_response);

        assert!(download_result.is_err());
    }

    fn trigger_download_with_supplied_response(
        request: UrlRequest,
        mut response: UrlResponse,
    ) -> Result<IndividualDownload, Error> {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (http_service, server) = create_http_service_util();
        let mut next_http_service_req = server.into_future();

        let url_target = (&request).url.clone();

        let fut = fetch_and_discard_url(&http_service, request);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        let (url_loader_responder, _service_control_handle) =
            match poll_http_service_request(&mut exec, &mut next_http_service_req) {
                Poll::Ready(HttpServiceRequest::CreateUrlLoader { loader, control_handle }) => {
                    (loader, control_handle)
                }
                Poll::Pending => panic!("expected something"),
            };
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        let mut next_url_loader_req = url_loader_responder
            .into_stream()
            .expect("failed to create a url_loader response stream")
            .into_future();

        let (url_request, url_request_responder) =
            match poll_url_loader_request(&mut exec, &mut next_url_loader_req) {
                Poll::Ready(UrlLoaderRequest::Start { request, responder }) => (request, responder),
                Poll::Pending => panic!("expected something"),
                _ => panic!("got something unexpected!"),
            };
        assert_eq!(url_target, url_request.url);

        url_request_responder.send(&mut response).expect("failed to send UrlResponse");

        let complete = exec.run_until_stalled(&mut fut);
        match complete {
            Poll::Ready(result) => result,
            Poll::Pending => panic!("future is pending and not ready"),
        }
    }

    fn create_url_response(
        error: Option<Box<HttpError>>,
        body: Option<Box<UrlBody>>,
        status_code: u32,
    ) -> http::UrlResponse {
        http::UrlResponse {
            error: error,
            body: body,
            url: None,
            status_code: status_code,
            status_line: None,
            headers: None,
            mime_type: None,
            charset: None,
            redirect_method: None,
            redirect_url: None,
            redirect_referrer: None,
        }
    }

    fn poll_http_service_request(
        exec: &mut fasync::Executor,
        next_http_service_req: &mut StreamFuture<HttpServiceRequestStream>,
    ) -> Poll<HttpServiceRequest> {
        exec.run_until_stalled(next_http_service_req).map(|(req, stream)| {
            *next_http_service_req = stream.into_future();
            req.expect("did not expect the HttpServiceRequestStream to end")
                .expect("error polling http service request stream")
        })
    }

    fn poll_url_loader_request(
        exec: &mut fasync::Executor,
        next_url_loader_req: &mut StreamFuture<UrlLoaderRequestStream>,
    ) -> Poll<UrlLoaderRequest> {
        exec.run_until_stalled(next_url_loader_req).map(|(req, stream)| {
            *next_url_loader_req = stream.into_future();
            req.expect("did not expect the UrlLoaderRequestStream to end")
                .expect("error polling url loader request stream")
        })
    }

    fn create_http_service_util() -> (HttpServiceProxy, HttpServiceRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<HttpServiceMarker>()
            .expect("falied to create a http_service_channel for tests");
        let server = server.into_stream().expect("failed to create a http_service response stream");
        (proxy, server)
    }
}
