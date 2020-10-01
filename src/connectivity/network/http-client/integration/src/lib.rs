// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fidl_fuchsia_net_http as http, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::StreamExt as _,
};

const ROOT_DOCUMENT: &str = "Root document\n";

fn big_vec() -> Vec<u8> {
    (0..(32usize << 20)).map(|n| n as u8).collect()
}

async fn run<F: futures::Future<Output = ()>>(
    func: impl Fn(http::LoaderProxy, std::net::SocketAddr) -> F,
) {
    use {
        futures::{select, FutureExt as _},
        rouille::router,
    };

    let server = rouille::Server::new("[::]:0", |request| {
        router!(request,
                (GET) (/) => {
                    rouille::Response::text(ROOT_DOCUMENT)
                },
                (GET) (/trigger_301) => {
                    rouille::Response::redirect_301("/")
                },
                (POST) (/see_other) => {
                    rouille::Response::redirect_303("/")
                },
                (GET) (/loop1) => {
                    rouille::Response::redirect_301("/loop2")
                },
                (GET) (/loop2) => {
                    rouille::Response::redirect_301("/loop1")
                },
                (GET) (/responds_in_10_minutes) => {
                    std::thread::sleep(std::time::Duration::from_secs(600));
                    rouille::Response::text(ROOT_DOCUMENT)
                },
                (GET) (/big_stream) => {
                    rouille::Response::from_data("application/octet-stream", big_vec())
                },
                _ => {
                    rouille::Response::empty_404()
                }
        )
    })
    .expect("failed to create rouille server");
    let launcher = fuchsia_component::client::launcher().expect("failed to open launcher service");
    let http_client = fuchsia_component::client::launch(
        &launcher,
        "fuchsia-pkg://fuchsia.com/http-client-integration-test#meta/http-client.cmx".to_string(),
        None,
    )
    .expect("failed to launch http client");
    let loader = http_client
        .connect_to_service::<http::LoaderMarker>()
        .expect("failed to connect to http client");

    select! {
        () = func(loader, server.server_addr()).fuse() => (),
        () = async {
            loop {
                let () = server.poll();
                // rouille isn't async, so we need to poll it until the test function completes.
                // Sleep every now and again to avoid scorching the CPU.
                let () = fasync::Timer::new(std::time::Duration::from_millis(10)).await;
            }
        }.fuse() => (),
    }
}

fn make_request(method: &str, url: String) -> http::Request {
    http::Request {
        url: Some(url),
        method: Some(method.to_string()),
        headers: None,
        body: None,
        deadline: None,
    }
}

fn check_response_common(response: &http::Response, expected_header_names: &[&str]) {
    assert_eq!(response.status_code, Some(200));
    // If the webserver started above ever returns different headers, or changes the order, this
    // assertion will fail. Note that case doesn't matter, and can vary across HTTP client
    // implementations, so we lowercase all the header keys before checking.
    let response_header_names = response
        .headers
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
    check_response_common(response, &["server", "date", "content-type", "content-length"]);
}

fn check_response_big(response: &http::Response) {
    check_response_common(response, &["server", "date", "content-type", "transfer-encoding"]);
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
            .fetch(make_request("GET", format!("http://{}", addr)))
            .await
            .expect("failed to fetch");
        let () = check_response(&response);
        let () = check_body(response.body, ROOT_DOCUMENT.as_bytes()).await;
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_past_deadline() {
    run(|loader, addr| async move {
        let response = loader
            .fetch({
                let mut req = make_request("GET", format!("http://{}", addr));

                // Deadline expired 10 minutes ago!
                req.deadline = Some(zx::Time::after(zx::Duration::from_minutes(-10)).into_nanos());
                req
            })
            .await
            .expect("failed to fetch");

        assert_eq!(response.error, Some(http::Error::DeadlineExceeded));
        assert!(response.body.is_none());
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_response_too_slow() {
    run(|loader, addr| async move {
        let response = loader
            .fetch({
                let mut req =
                    make_request("GET", format!("http://{}/responds_in_10_minutes", addr));
                // Deadline expires 100ms from now.
                req.deadline = Some(zx::Time::after(zx::Duration::from_millis(100)).into_nanos());
                req
            })
            .await
            .expect("failed to fetch");

        assert_eq!(response.error, Some(http::Error::DeadlineExceeded));
        assert!(response.body.is_none());
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_https() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("GET", format!("https://{}", addr)))
            .await
            .expect("failed to fetch");

        assert_eq!(response.error, Some(http::Error::Connect));
        assert!(response.body.is_none());
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_start_http() {
    run(|loader, addr| async move {
        let (tx, rx) = fidl::endpoints::create_endpoints().expect("failed to create endpoints");

        let () = loader
            .start(make_request("GET", format!("http://{}", addr)), tx)
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
        let () = check_body(response.body, ROOT_DOCUMENT.as_bytes()).await;

        let () = responder.send().expect("failed to respond");
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_redirect() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("GET", format!("http://{}/trigger_301", addr)))
            .await
            .expect("failed to fetch");
        assert_eq!(response.final_url, Some(format!("http://{}/", addr)));
        let () = check_response(&response);
        let () = check_body(response.body, ROOT_DOCUMENT.as_bytes()).await;
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_start_redirect() {
    run(|loader, addr| async move {
        let (tx, rx) = fidl::endpoints::create_endpoints().expect("failed to create endpoints");

        let () = loader
            .start(make_request("GET", format!("http://{}/trigger_301", addr)), tx)
            .expect("failed to start");

        let mut rx = rx.into_stream().expect("failed to convert to stream");

        let (response, responder) = rx
            .next()
            .await
            .expect("stream error")
            .expect("request error")
            .into_on_response()
            .expect("failed to convert to event stream");

        assert_eq!(response.status_code, Some(301));
        assert_eq!(
            response.redirect,
            Some(http::RedirectTarget {
                method: Some("GET".to_string()),
                url: Some(format!("http://{}/", addr)),
                referrer: None,
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

        assert_eq!(response.final_url, Some(format!("http://{}/", addr)));
        let () = check_response(&response);
        let () = check_body(response.body, ROOT_DOCUMENT.as_bytes()).await;

        let () = responder.send().expect("failed to respond");
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_see_other() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("POST", format!("http://{}/see_other", addr)))
            .await
            .expect("failed to fetch");
        assert_eq!(response.final_url, Some(format!("http://{}/", addr)));
        let () = check_response(&response);
        let () = check_body(response.body, ROOT_DOCUMENT.as_bytes()).await;
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_start_see_other() {
    run(|loader, addr| async move {
        let (tx, rx) = fidl::endpoints::create_endpoints().expect("failed to create endpoints");

        let () = loader
            .start(make_request("POST", format!("http://{}/see_other", addr)), tx)
            .expect("failed to start");

        let mut rx = rx.into_stream().expect("failed to convert to stream");

        let (response, responder) = rx
            .next()
            .await
            .expect("stream error")
            .expect("request error")
            .into_on_response()
            .expect("failed to convert to event stream");

        assert_eq!(response.status_code, Some(303));
        assert_eq!(
            response.redirect,
            Some(http::RedirectTarget {
                method: Some("GET".to_string()),
                url: Some(format!("http://{}/", addr)),
                referrer: None,
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

        assert_eq!(response.final_url, Some(format!("http://{}/", addr)));
        let () = check_response(&response);
        let () = check_body(response.body, ROOT_DOCUMENT.as_bytes()).await;

        let () = responder.send().expect("failed to respond");
    })
    .await
}

#[fasync::run_singlethreaded(test)]
async fn test_fetch_max_redirect() {
    run(|loader, addr| async move {
        let response = loader
            .fetch(make_request("GET", format!("http://{}/loop1", addr)))
            .await
            .expect("failed to fetch");
        // The last request in the redirect loop will always return status code 301
        assert_eq!(response.status_code, Some(301));
        assert_eq!(
            response.redirect,
            Some(http::RedirectTarget {
                method: Some("GET".to_string()),
                url: Some(format!("http://{}/loop2", addr)),
                referrer: None,
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
            .start(make_request("GET", format!("http://{}/loop1", addr)), tx)
            .expect("failed to start");

        let mut rx = rx.into_stream().expect("failed to convert to stream");

        for () in std::iter::repeat(()).take(3) {
            let (response, responder) = rx
                .next()
                .await
                .expect("stream error")
                .expect("request error")
                .into_on_response()
                .expect("failed to convert to event stream");

            assert_eq!(response.status_code, Some(301));
            assert_eq!(
                response.redirect,
                Some(http::RedirectTarget {
                    method: Some("GET".to_string()),
                    url: Some(format!("http://{}/loop2", addr)),
                    referrer: None,
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

            assert_eq!(response.status_code, Some(301));
            assert_eq!(
                response.redirect,
                Some(http::RedirectTarget {
                    method: Some("GET".to_string()),
                    url: Some(format!("http://{}/loop1", addr)),
                    referrer: None,
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
            .fetch(make_request("GET", format!("http://{}/big_stream", addr)))
            .await
            .expect("failed to fetch");

        let () = check_response_big(&response);
        let () = check_body(response.body, &big_vec()).await;
    })
    .await
}
