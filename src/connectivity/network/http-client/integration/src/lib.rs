// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use fidl_fuchsia_net_http as http;
use fuchsia_async as fasync;
use fuchsia_component_test::ScopedInstance;
use fuchsia_zircon as zx;
use futures::{FutureExt as _, StreamExt as _, TryStreamExt as _};
use std::future::Future;
use std::net::SocketAddr;

const ROOT_DOCUMENT: &str = "Root document\n";

fn big_vec() -> Vec<u8> {
    (0..(32usize << 20)).map(|n| n as u8).collect()
}

const TRIGGER_301: &str = "/trigger_301";
const SEE_OTHER: &str = "/see_other";
const LOOP1: &str = "/loop1";
const LOOP2: &str = "/loop2";
const PENDING: &str = "/pending";
const BIG_STREAM: &str = "/big_stream";

async fn run<F: Future<Output = ()>>(func: impl Fn(http::LoaderProxy, SocketAddr) -> F) {
    use futures::future::Either;
    use hyper::{
        header,
        service::{make_service_fn, service_fn},
        Body, Method, Response, StatusCode,
    };
    use std::convert::{Infallible, TryInto as _};
    use std::net::{IpAddr, Ipv6Addr};

    let addr = SocketAddr::new(IpAddr::V6(Ipv6Addr::UNSPECIFIED), 0);
    let listener = fasync::net::TcpListener::bind(&addr).expect("bind");
    let server_addr = listener.local_addr().expect("local address");
    let listener = listener
        .accept_stream()
        .map_ok(|(stream, _): (_, SocketAddr)| fuchsia_hyper::TcpStream { stream });

    const ROOT: &str = "/";
    let root = || ROOT.try_into().expect("root location");
    let loop1 = || LOOP1.try_into().expect("loop1 location");
    let loop2 = || LOOP2.try_into().expect("loop2 location");

    let svc = service_fn(move |req| {
        let ready = |t| std::future::ready(t).left_future();
        let pending = std::future::pending().right_future();

        match (req.method(), req.uri().path()) {
            (&Method::GET, ROOT) => {
                let response = Response::new(ROOT_DOCUMENT.into());
                ready(response)
            }
            (&Method::GET, TRIGGER_301) => {
                let mut response = Response::new(Body::empty());
                *response.status_mut() = StatusCode::MOVED_PERMANENTLY;
                assert_eq!(response.headers_mut().insert(header::LOCATION, root()), None);
                ready(response)
            }
            (&Method::POST, SEE_OTHER) => {
                let mut response = Response::new(Body::empty());
                *response.status_mut() = StatusCode::SEE_OTHER;
                assert_eq!(response.headers_mut().insert(header::LOCATION, root()), None);
                ready(response)
            }
            (&Method::GET, LOOP1) => {
                let mut response = Response::new(Body::empty());
                *response.status_mut() = StatusCode::MOVED_PERMANENTLY;
                assert_eq!(response.headers_mut().insert(header::LOCATION, loop2()), None);
                ready(response)
            }
            (&Method::GET, LOOP2) => {
                let mut response = Response::new(Body::empty());
                *response.status_mut() = StatusCode::MOVED_PERMANENTLY;
                assert_eq!(response.headers_mut().insert(header::LOCATION, loop1()), None);
                ready(response)
            }
            (&Method::GET, PENDING) => pending,
            (&Method::GET, BIG_STREAM) => {
                let encoding = "application/octet-stream".try_into().expect("octet stream");
                let mut response = Response::new(big_vec().into());
                assert_eq!(
                    response.headers_mut().insert(header::TRANSFER_ENCODING, encoding),
                    None
                );
                ready(response)
            }
            _ => {
                let mut response = Response::new(Body::empty());
                *response.status_mut() = StatusCode::NOT_FOUND;
                ready(response)
            }
        }
        .map(Ok::<_, Infallible>)
    });

    let make_svc = make_service_fn(move |_: &fuchsia_hyper::TcpStream| {
        futures::future::ok::<_, Infallible>(svc)
    });

    let server = hyper::Server::builder(hyper::server::accept::from_stream(listener))
        .executor(fuchsia_hyper::Executor)
        .serve(make_svc);

    const HTTP_CLIENT_URL: &str = "#meta/http-client.cm";
    let http_client = ScopedInstance::new("coll".into(), HTTP_CLIENT_URL.into())
        .await
        .expect("failed to create http-client");
    let loader = http_client
        .connect_to_protocol_at_exposed_dir::<http::LoaderMarker>()
        .expect("failed to connect to http client");

    let fut = func(loader, server_addr);
    futures::pin_mut!(fut);
    match futures::future::select(fut, server).await {
        Either::Left(((), _server)) => {}
        Either::Right((result, _fut)) => {
            panic!("hyper server exited: {:?}", result);
        }
    }
}

fn make_request(method: &str, url: String, deadline: Option<zx::Time>) -> http::Request {
    // Unless specified by the caller, use an infinite timeout to avoid flakes.
    let deadline = deadline.unwrap_or(zx::Time::INFINITE);
    http::Request {
        url: Some(url),
        method: Some(method.to_string()),
        headers: None,
        body: None,
        deadline: Some(deadline.into_nanos()),
        ..http::Request::EMPTY
    }
}

fn check_response_common(response: &http::Response, expected_header_names: &[&str]) {
    let http::Response { error, status_code, headers, .. } = response;
    assert_eq!(error, &None);
    assert_eq!(status_code, &Some(200));
    // If the webserver started above ever returns different headers, or changes the order, this
    // assertion will fail. Note that case doesn't matter, and can vary across HTTP client
    // implementations, so we lowercase all the header keys before checking.
    let response_header_names = headers
        .as_ref()
        .map(|headers| {
            headers
                .iter()
                .map(|http::Header { name, value: _ }| {
                    std::str::from_utf8(name).map(str::to_lowercase)
                })
                .collect::<Result<Vec<_>, _>>()
        })
        .transpose();
    let response_header_names = response_header_names.as_ref().map(|option| {
        option.as_ref().map(|vec| vec.iter().map(std::string::String::as_str).collect::<Vec<_>>())
    });
    let response_header_names =
        response_header_names.as_ref().map(|option| option.as_ref().map(|vec| vec.as_slice()));
    assert_eq!(response_header_names, Ok(Some(expected_header_names)));
}

fn check_response(response: &http::Response) {
    check_response_common(response, &["content-length", "date"]);
}

fn check_response_big(response: &http::Response) {
    check_response_common(response, &["transfer-encoding", "date"]);
}

async fn check_body(body: Option<zx::Socket>, mut expected: &[u8]) {
    use futures::AsyncReadExt as _;

    let body = body.expect("response did not include body socket");
    let mut body = fasync::Socket::from_socket(body).expect("failed to create async socket");

    let mut buf = [0; 1024];
    while !expected.is_empty() {
        let n = body.read(&mut buf).await.expect("failed to read from response body socket");
        assert_eq!(buf[..n], expected[..n]);
        expected = &expected[n..];
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_http() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("GET", format!("http://{}", addr), None))
            .await
            .expect("failed to fetch");
        let () = check_response(&response);
        let http::Response { body, .. } = response;
        let () = check_body(body, ROOT_DOCUMENT.as_bytes()).await;
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_past_deadline() {
    run(|loader, addr| async move {
        // Deadline expired 10 minutes ago!
        let deadline = Some(zx::Time::after(zx::Duration::from_minutes(-10)));
        let http::Response { error, body, .. } = loader
            .fetch(make_request("GET", format!("http://{}", addr), deadline))
            .await
            .expect("failed to fetch");

        assert_eq!(error, Some(http::Error::DeadlineExceeded));
        assert_eq!(body, None);
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_response_too_slow() {
    run(|loader, addr| async move {
        // Deadline expires 100ms from now.
        let deadline = Some(zx::Time::after(zx::Duration::from_millis(100)));
        let http::Response { error, body, .. } = loader
            .fetch(make_request("GET", format!("http://{}{}", addr, PENDING), deadline))
            .await
            .expect("failed to fetch");

        assert_eq!(error, Some(http::Error::DeadlineExceeded));
        assert_eq!(body, None);
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_https() {
    run(|loader, addr| async move {
        let http::Response { error, body, .. } = loader
            .fetch(make_request("GET", format!("https://{}", addr), None))
            .await
            .expect("failed to fetch");

        assert_eq!(error, Some(http::Error::Connect));
        assert_eq!(body, None);
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_start_http() {
    run(|loader, addr| async move {
        let (tx, rx) = fidl::endpoints::create_endpoints().expect("failed to create endpoints");

        let () = loader
            .start(make_request("GET", format!("http://{}", addr), None), tx)
            .expect("failed to start");

        let mut rx = rx.into_stream().expect("failed to convert to stream");

        let (response, responder) = rx
            .next()
            .await
            .expect("stream error")
            .expect("request error")
            .into_on_response()
            .expect("failed to convert to event stream");

        let () = check_response(&response);
        let http::Response { body, .. } = response;
        let () = check_body(body, ROOT_DOCUMENT.as_bytes()).await;

        let () = responder.send().expect("failed to respond");
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_redirect() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("GET", format!("http://{}{}", addr, TRIGGER_301), None))
            .await
            .expect("failed to fetch");
        let () = check_response(&response);
        let http::Response { body, final_url, .. } = response;
        let () = check_body(body, ROOT_DOCUMENT.as_bytes()).await;
        assert_eq!(final_url, Some(format!("http://{}/", addr)));
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_start_redirect() {
    run(|loader, addr| async move {
        let (tx, rx) = fidl::endpoints::create_endpoints().expect("failed to create endpoints");

        let () = loader
            .start(make_request("GET", format!("http://{}{}", addr, TRIGGER_301), None), tx)
            .expect("failed to start");

        let mut rx = rx.into_stream().expect("failed to convert to stream");

        let (http::Response { status_code, redirect, .. }, responder) = rx
            .next()
            .await
            .expect("stream error")
            .expect("request error")
            .into_on_response()
            .expect("failed to convert to event stream");

        assert_eq!(status_code, Some(301));
        assert_eq!(
            redirect,
            Some(http::RedirectTarget {
                method: Some("GET".to_string()),
                url: Some(format!("http://{}/", addr)),
                referrer: None,
                ..http::RedirectTarget::EMPTY
            })
        );

        let () = responder.send().expect("failed to respond");

        let (response, responder) = rx
            .next()
            .await
            .expect("stream error")
            .expect("request error")
            .into_on_response()
            .expect("failed to convert to event stream");
        let () = check_response(&response);
        let http::Response { body, final_url, .. } = response;
        let () = check_body(body, ROOT_DOCUMENT.as_bytes()).await;
        assert_eq!(final_url, Some(format!("http://{}/", addr)));

        let () = responder.send().expect("failed to respond");
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_see_other() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("POST", format!("http://{}{}", addr, SEE_OTHER), None))
            .await
            .expect("failed to fetch");
        let () = check_response(&response);
        let http::Response { body, final_url, .. } = response;
        let () = check_body(body, ROOT_DOCUMENT.as_bytes()).await;
        assert_eq!(final_url, Some(format!("http://{}/", addr)));
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_start_see_other() {
    run(|loader, addr| async move {
        let (tx, rx) = fidl::endpoints::create_endpoints().expect("failed to create endpoints");

        let () = loader
            .start(make_request("POST", format!("http://{}{}", addr, SEE_OTHER), None), tx)
            .expect("failed to start");

        let mut rx = rx.into_stream().expect("failed to convert to stream");

        let (http::Response { status_code, redirect, .. }, responder) = rx
            .next()
            .await
            .expect("stream error")
            .expect("request error")
            .into_on_response()
            .expect("failed to convert to event stream");

        assert_eq!(status_code, Some(303));
        assert_eq!(
            redirect,
            Some(http::RedirectTarget {
                method: Some("GET".to_string()),
                url: Some(format!("http://{}/", addr)),
                referrer: None,
                ..http::RedirectTarget::EMPTY
            })
        );

        let () = responder.send().expect("failed to respond");

        let (response, responder) = rx
            .next()
            .await
            .expect("stream error")
            .expect("request error")
            .into_on_response()
            .expect("failed to convert to event stream");

        let () = check_response(&response);
        let http::Response { body, final_url, .. } = response;
        let () = check_body(body, ROOT_DOCUMENT.as_bytes()).await;
        assert_eq!(final_url, Some(format!("http://{}/", addr)));

        let () = responder.send().expect("failed to respond");
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_max_redirect() {
    run(|loader, addr| async move {
        let http::Response { status_code, redirect, .. } = loader
            .fetch(make_request("GET", format!("http://{}{}", addr, LOOP1), None))
            .await
            .expect("failed to fetch");
        // The last request in the redirect loop will always return status code 301
        assert_eq!(status_code, Some(301));
        assert_eq!(
            redirect,
            Some(http::RedirectTarget {
                method: Some("GET".to_string()),
                url: Some(format!("http://{}{}", addr, LOOP2)),
                referrer: None,
                ..http::RedirectTarget::EMPTY
            })
        );
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_start_redirect_loop() {
    run(|loader, addr| async move {
        let (tx, rx) = fidl::endpoints::create_endpoints().expect("failed to create endpoints");

        let () = loader
            .start(make_request("GET", format!("http://{}{}", addr, LOOP1), None), tx)
            .expect("failed to start");

        let mut rx = rx.into_stream().expect("failed to convert to stream");

        for () in std::iter::repeat(()).take(3) {
            let (http::Response { status_code, redirect, .. }, responder) = rx
                .next()
                .await
                .expect("stream error")
                .expect("request error")
                .into_on_response()
                .expect("failed to convert to event stream");

            assert_eq!(status_code, Some(301));
            assert_eq!(
                redirect,
                Some(http::RedirectTarget {
                    method: Some("GET".to_string()),
                    url: Some(format!("http://{}{}", addr, LOOP2)),
                    referrer: None,
                    ..http::RedirectTarget::EMPTY
                })
            );

            let () = responder.send().expect("failed to respond");

            let (http::Response { status_code, redirect, .. }, responder) = rx
                .next()
                .await
                .expect("stream error")
                .expect("request error")
                .into_on_response()
                .expect("failed to convert to event stream");

            assert_eq!(status_code, Some(301));
            assert_eq!(
                redirect,
                Some(http::RedirectTarget {
                    method: Some("GET".to_string()),
                    url: Some(format!("http://{}{}", addr, LOOP1)),
                    referrer: None,
                    ..http::RedirectTarget::EMPTY
                })
            );

            let () = responder.send().expect("failed to respond");
        }
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_http_big_stream() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("GET", format!("http://{}{}", addr, BIG_STREAM), None))
            .await
            .expect("failed to fetch");

        let () = check_response_big(&response);
        let http::Response { body, .. } = response;
        let () = check_body(body, &big_vec()).await;
    })
    .await
}
